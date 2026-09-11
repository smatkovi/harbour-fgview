/* harbour-fgview - Anzeige und Bedienung fuer FlightGear auf SailfishOS
 *
 * Liest die von GraphicsWindowEGL geschriebenen Frames aus
 * /dev/shm/fgfs-frame und stellt sie dar. Sensoren und
 * Bedienelemente werden per UDP an FlightGears generic-Protokoll
 * geschickt (Protocol/fgtouch.xml, Port 5501).
 */

#include <QtQuick>
#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QSGSimpleTextureNode>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QPair>
#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <QAccelerometer>
#include <cmath>
#include <QGyroscope>
#include <QtMath>

#include <sailfishapp.h>
#include "fgruntime.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

// ===================================================================
//  Shared-Memory-Leser
// ===================================================================

#pragma pack(push, 1)
struct FgFrameHeader {
    quint32 magic;
    quint32 width;
    quint32 height;
    quint32 bpp;
    quint64 sequence;
    quint32 activeSlot;
    quint32 reserved;
    /* Zero-Copy: Verweis auf den dmabuf, in den die GPU rendert.
       dmabufPid == 0 bedeutet klassischer Readback. */
    quint32 dmabufPid;
    quint32 dmabufFd;
    quint32 dmabufStride;
    quint32 dmabufFourcc;
};
#pragma pack(pop)

static const quint32 FGFR_MAGIC = 0x46474652u;
static const int     FGFR_HDR   = 48;

class FrameItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(int  fps       READ fps       NOTIFY fpsChanged)

public:
    FrameItem(QQuickItem* parent = nullptr) : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);

        connect(&_poll, &QTimer::timeout, this, &FrameItem::poll);
        _poll.start(33);                 // 30 Hz Abfrage

        connect(&_fpsTimer, &QTimer::timeout, this, [this]{
            _fps = _framesSinceTick;
            _framesSinceTick = 0;
            emit fpsChanged();
        });
        _fpsTimer.start(1000);
        _report.start();
    }

    ~FrameItem() override { closeShm(); }

    bool connected() const { return _base != nullptr; }
    int  fps() const { return _fps; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override
    {
        if (_image.isNull()) { delete old; return nullptr; }
        QElapsedTimer tn; tn.start();

        QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(old);
        if (!node) {
            node = new QSGSimpleTextureNode();
            node->setFiltering(QSGTexture::Linear);
            /* GL rendert von unten nach oben; die Spiegelung hier
               kostet nichts, im memcpy waere sie teuer. */
            node->setTextureCoordinatesTransform(
                QSGSimpleTextureNode::MirrorVertically);
        }

        /* Textur nur neu anlegen, wenn sich die Groesse geaendert hat.
           Sonst denselben Speicher ueberschreiben. */
        if (!_texture || _texture->textureSize() != _image.size()) {
            delete _texture;
            _texture = window()->createTextureFromImage(
                _image, QQuickWindow::TextureIsOpaque);
            node->setTexture(_texture);
        } else {
            QElapsedTimer tt; tt.start();
            QSGTexture* fresh = window()->createTextureFromImage(
                _image, QQuickWindow::TextureIsOpaque);
            _tTex += tt.nsecsElapsed();
            ++_nTex;
            delete _texture;
            _texture = fresh;
            node->setTexture(_texture);
        }

        // Seitenverhaeltnis wahren, zentriert einpassen
        const qreal iw = _image.width(), ih = _image.height();
        const qreal sw = width(), sh = height();
        if (iw <= 0 || ih <= 0 || sw <= 0 || sh <= 0) return node;

        const qreal scale = qMin(sw / iw, sh / ih);
        const qreal dw = iw * scale, dh = ih * scale;
        node->setRect((sw - dw) / 2.0, (sh - dh) / 2.0, dw, dh);
        _tNode += tn.nsecsElapsed();
        ++_nNode;
        return node;
    }

private slots:
    void poll()
    {
        if (!_base && !openShm()) return;
        QElapsedTimer t; t.start();

        const FgFrameHeader* hdr =
            reinterpret_cast<const FgFrameHeader*>(_base);

        // seqlock: vor und nach dem Kopieren lesen
        const quint64 s1 = hdr->sequence;
        if (s1 & 1u) return;                    // Schreibvorgang laeuft
        if (s1 == _lastSeq) return;             // kein neuer Frame

        const int w = int(hdr->width);
        const int h = int(hdr->height);
        const int slot = int(hdr->activeSlot);
        const size_t bytes = size_t(w) * size_t(h) * 4u;

        if (_image.width() != w || _image.height() != h)
            _image = QImage(w, h, QImage::Format_RGBA8888);

        /* Zero-Copy: der Simulator rendert direkt in einen dmabuf,
           den wir ueber /proc/<pid>/fd/<fd> oeffnen und mappen. */
        const uchar* src = nullptr;
        if (hdr->dmabufPid != 0) {
            if (!_dmabuf && !openDmabuf(hdr->dmabufPid, hdr->dmabufFd, bytes))
                return;                      /* nicht erreichbar */
            src = _dmabuf;
        } else {
            src = _base + FGFR_HDR + size_t(slot) * bytes;
        }

        /* Auf den Fence warten, falls der Simulator einen liefert.
           Das haelt uns auf, nicht den Simulator - dessen Pipeline
           bleibt gefuellt. */
        waitForFence();

        /* Ein Durchgang statt 768 Einzelkopien. Dass GL von unten
           nach oben liefert, gleicht der Szenengraph beim Zeichnen
           aus - siehe setTextureCoordinatesTransform unten. */
        memcpy(_image.bits(), src, bytes);

        const quint64 s2 = hdr->sequence;
        if (s2 != s1) return;                   // Frame war inkonsistent

        _lastSeq = s1;
        ++_framesSinceTick;

        _tPoll += t.nsecsElapsed();
        ++_nPoll;
        if (_report.elapsed() > 2000) {
            const double f = 1.0e6;   /* ns -> ms */
            qWarning("FGVIEW timing: poll %.1f ms  tex %.1f ms  node %.1f ms  "
                     "(%d Frames in 2 s)",
                     _nPoll  ? _tPoll  / f / _nPoll  : 0.0,
                     _nTex   ? _tTex   / f / _nTex   : 0.0,
                     _nNode  ? _tNode  / f / _nNode  : 0.0,
                     _nPoll);
            _tPoll = _tTex = _tNode = 0;
            _nPoll = _nTex = _nNode = 0;
            _report.restart();
        }

        update();
    }

private:
    /* Der Deskriptor kommt ueber einen Unix-Socket mit SCM_RIGHTS.
       Ueber /proc/<pid>/fd/ ginge es nicht: fuer anonyme Inodes wie
       dmabuf lehnt der Kernel das Wiederoeffnen mit ENXIO ab. */
    /* kind: 'F' = dmabuf, 'S' = Fence. */
    static int receiveFd(const char* path, char* kind = nullptr)
    {
        int s = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s < 0) return -1;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (::connect(s, (struct sockaddr*)&addr, sizeof addr) < 0) {
            ::close(s);
            return -1;
        }

        char dummy = 0;
        struct iovec iov;
        iov.iov_base = &dummy;
        iov.iov_len = 1;

        char cbuf[CMSG_SPACE(sizeof(int))];
        memset(cbuf, 0, sizeof cbuf);

        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof cbuf;

        const ssize_t n = ::recvmsg(s, &msg, 0);
        int fd = -1;
        if (n > 0 && kind) *kind = dummy;
        if (n > 0) {
            for (struct cmsghdr* cm = CMSG_FIRSTHDR(&msg); cm;
                 cm = CMSG_NXTHDR(&msg, cm)) {
                if (cm->cmsg_level == SOL_SOCKET &&
                    cm->cmsg_type == SCM_RIGHTS) {
                    memcpy(&fd, CMSG_DATA(cm), sizeof(int));
                    break;
                }
            }
        }
        ::close(s);
        return fd;
    }

    bool openDmabuf(quint32 pid, quint32 fd, size_t bytes)
    {
        if (_dmabufTried == QPair<quint32,quint32>(pid, fd))
            return _dmabuf != nullptr;
        _dmabufTried = QPair<quint32,quint32>(pid, fd);

        closeDmabuf();

        const QByteArray sock =
            qEnvironmentVariableIsSet("FGFS_FD_SOCKET")
                ? qgetenv("FGFS_FD_SOCKET")
                : QByteArray("/tmp/fgfs-frame.sock");

        int d = receiveFd(sock.constData());
        if (d < 0) {
            qWarning("FGVIEW: kein Deskriptor ueber %s", sock.constData());
            return false;
        }
        void* p = ::mmap(nullptr, bytes, PROT_READ, MAP_SHARED, d, 0);
        ::close(d);
        if (p == MAP_FAILED) {
            qWarning("FGVIEW: mmap des dmabuf fehlgeschlagen");
            return false;
        }
        _dmabuf = static_cast<uchar*>(p);
        _dmabufBytes = bytes;
        qWarning("FGVIEW: Zero-Copy aktiv, %zu KiB gemappt", bytes / 1024);
        return true;
    }

    void waitForFence()
    {
        if (!_fenceAvailable) return;

        const QByteArray sock =
            qEnvironmentVariableIsSet("FGFS_FD_SOCKET")
                ? qgetenv("FGFS_FD_SOCKET")
                : QByteArray("/tmp/fgfs-frame.sock");

        char kind = 0;
        const int f = receiveFd(sock.constData(), &kind);
        if (f < 0 || kind != 'S') {
            if (f >= 0) ::close(f);
            _fenceMisses++;
            if (_fenceMisses > 30) {
                _fenceAvailable = false;   /* Simulator liefert keine */
                qWarning("FGVIEW: kein Fence, lese ungesynct");
            }
            return;
        }
        _fenceMisses = 0;

        struct pollfd pfd;
        pfd.fd = f;
        pfd.events = POLLIN;
        ::poll(&pfd, 1, 100);              /* hoechstens 100 ms */
        ::close(f);
    }

    void closeDmabuf()
    {
        if (_dmabuf) { ::munmap(_dmabuf, _dmabufBytes); _dmabuf = nullptr; }
        _dmabufBytes = 0;
    }

    bool openShm()
    {
        int fd = ::shm_open("/fgfs-frame", O_RDONLY, 0);
        if (fd < 0) return false;

        // erst nur den Header lesen, um die Groesse zu erfahren
        void* p = ::mmap(nullptr, FGFR_HDR, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) { ::close(fd); return false; }

        const FgFrameHeader* hdr = static_cast<const FgFrameHeader*>(p);
        if (hdr->magic != FGFR_MAGIC || hdr->width == 0 || hdr->height == 0) {
            ::munmap(p, FGFR_HDR);
            ::close(fd);
            return false;
        }
        const size_t total =
            FGFR_HDR + 2u * size_t(hdr->width) * size_t(hdr->height) * 4u;
        ::munmap(p, FGFR_HDR);

        void* full = ::mmap(nullptr, total, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (full == MAP_FAILED) return false;

        _base = static_cast<uchar*>(full);
        _mapped = total;
        emit connectedChanged();
        return true;
    }

    void closeShm()
    {
        closeDmabuf();
        if (_base) { ::munmap(_base, _mapped); _base = nullptr; }
        if (_texture) { delete _texture; _texture = nullptr; }
    }

signals:
    void connectedChanged();
    void fpsChanged();

private:
    uchar*  _base = nullptr;
    size_t  _mapped = 0;
    uchar*  _dmabuf = nullptr;
    bool    _fenceAvailable = true;
    int     _fenceMisses = 0;
    size_t  _dmabufBytes = 0;
    QPair<quint32,quint32> _dmabufTried = qMakePair(0u, 0u);
    quint64 _lastSeq = 0;
    QImage  _image;
    QSGTexture* _texture = nullptr;
    QTimer  _poll, _fpsTimer;
    qint64  _tPoll = 0, _tTex = 0, _tNode = 0;
    int     _nPoll = 0, _nTex = 0, _nNode = 0;
    QElapsedTimer _report;
    int _fps = 0, _framesSinceTick = 0;
};


// ===================================================================
//  Steuerung: Sensoren + Bedienelemente -> UDP
// ===================================================================

class ControlSender : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal throttle READ throttle WRITE setThrottle NOTIFY changed)
    /* FlightGear's tutorials (interactive lessons), read from the running
       simulator; the current instruction while one runs */
    Q_PROPERTY(QVariantList tutorials READ tutorials NOTIFY tutorialsChanged)
    Q_PROPERTY(bool tutorialRunning READ tutorialRunning NOTIFY tutorialChanged)
    Q_PROPERTY(QString tutorialMessage READ tutorialMessage NOTIFY tutorialChanged)
    Q_PROPERTY(QString tutorialName READ tutorialName NOTIFY tutorialChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool reversing READ reversing NOTIFY changed)
    Q_PROPERTY(qreal reverseDepth READ reverseDepth NOTIFY changed)
    Q_PROPERTY(qreal rudder   READ rudder   WRITE setRudder   NOTIFY changed)
    Q_PROPERTY(qreal flaps    READ flaps    WRITE setFlaps    NOTIFY changed)
    Q_PROPERTY(qreal brake    READ brake    WRITE setBrake    NOTIFY changed)
    Q_PROPERTY(bool  gearDown READ gearDown WRITE setGearDown NOTIFY changed)
    Q_PROPERTY(qreal aileron  READ aileron  NOTIFY changed)
    Q_PROPERTY(qreal elevator READ elevator NOTIFY changed)
    Q_PROPERTY(bool  tiltActive READ tiltActive WRITE setTiltActive NOTIFY changed)
    Q_PROPERTY(bool  cranking   READ cranking   NOTIFY changed)
    Q_PROPERTY(bool  engineOn   READ engineOn   NOTIFY changed)

public:
    explicit ControlSender(QObject* parent = nullptr) : QObject(parent)
    {
        /* the current instruction of a running lesson, every 1.5 s */
        _tutorialPoll.setInterval(1500);
        connect(&_tutorialPoll, &QTimer::timeout, this, &ControlSender::pollTutorial);
        /* coalesced view updates from a drag, see setViewOffsets() */
        _viewTimer.setSingleShot(true);
        connect(&_viewTimer, &QTimer::timeout, this, &ControlSender::flushViewOffsets);
        _accel.setDataRate(50);
        _accel.start();

        connect(&_tick, &QTimer::timeout, this, &ControlSender::sendPacket);
        _tick.start(33);                 // ~30 Hz
    }

    qreal throttle() const { return _throttle; }
    qreal rudder()   const { return _rudder; }
    qreal flaps()    const { return _flaps; }
    qreal brake()    const { return _brake; }
    bool  gearDown() const { return _gearDown; }
    qreal aileron()  const { return _aileron; }
    qreal elevator() const { return _elevator; }
    bool  tiltActive() const { return _tiltActive; }
    bool  cranking() const { return _cranking; }
    bool  engineOn() const { return _engineOn; }
    QVariantList tutorials() const { return _tutorials; }
    bool  tutorialRunning() const { return _tutorialRunning; }
    QString tutorialMessage() const { return _tutorialMessage; }
    QString tutorialName() const { return _tutorialName; }
    bool  paused() const { return _paused; }
    bool  reversing() const { return _reversing; }
    qreal reverseDepth() const { return _reverseDepth; }

    void setThrottle(qreal v) { _throttle = clamp01(v); emit changed(); }
    void setRudder(qreal v)   { _rudder = clamp11(v);   emit changed(); }
    void setFlaps(qreal v)    { _flaps = clamp01(v);    emit changed(); }
    void setBrake(qreal v)
    {
        _brake = clamp01(v);
        /* Radbremsen liegen ausserhalb des generic-Protokolls */
        const QString b = QString::number(_brake, 'f', 2);
        sendTelnet(QStringList()
                   << "set /controls/gear/brake-left " + b
                   << "set /controls/gear/brake-right " + b);
        emit changed();
    }
    void setGearDown(bool v)  { _gearDown = v;          emit changed(); }
    void setTiltActive(bool v){ _tiltActive = v;        emit changed(); }

public slots:
    /* The whole start-up, for whatever is loaded, as one Nasal script over
       FlightGear's telnet channel (the generic protocol only sets the
       axes; switches, fuel and starters need property access, and the
       number of engines is only known inside the simulator).

       Every engine gets the piston items (magnetos, mixture, primer) and
       the turbine items (cutoff off, starter on); each engine model reads
       what it knows and ignores the rest.  The starters then stay engaged
       until that engine reports running, up to ninety seconds: a JSBSim
       turbine aborts the start the moment the starter drops, and the
       eight fixed seconds of the old sequence, sized for the c172p, were
       far too short for a spooling jet - which is why the A320's engines
       never came up, and why only engine[0] was ever cranked.

       Aircraft that bring their own start-up automation are handed to
       it: the A320 family's acconfig "ready for taxi" state runs APU,
       bleed air and both engines in the right order, which the FADEC
       insists on. */
    void startEngine()
    {
        if (_cranking) return;

        sendNasal(QByteArray(
            "# The aircraft's own start-up procedure comes first: the c172p's\n"
            "# autostart resets battery and breakers and sets the lights, the\n"
            "# ec135's startup releases the rotor brake, arms both FADECs and\n"
            "# walks the power levers - none of which a generic script knows.\n"
            "# Searched in the aircraft's own Nasal namespaces, not FGData's.\n"
            "var core = {tutorial:1, local_weather:1, jetways:1, jetways_edit:1, FailureMgr:1,\n"
            "            canvas:1, console:1, debug:1, input_helpers:1, performance_monitor:1,\n"
            "            std:1, towing:1, Autopush:1, modules:1};\n"
            "var fgtouch_own = func(names) {\n"
            "    var nasalN = props.globals.getNode(\"/nasal\");\n"
            "    if (nasalN == nil) return \"\";\n"
            "    foreach (var c; nasalN.getChildren()) {\n"
            "        var ns = c.getName();\n"
            "        if (contains(core, ns) or !contains(globals, ns) or typeof(globals[ns]) != \"hash\") continue;\n"
            "        foreach (var fn; names)\n"
            "            if (contains(globals[ns], fn) and typeof(globals[ns][fn]) == \"func\") {\n"
            "                var err = [];\n"
            "                call(globals[ns][fn], [], nil, nil, err);\n"
            "                if (size(err) == 0) return ns ~ \".\" ~ fn;\n"
            "            }\n"
            "    }\n"
            "    return \"\";\n"
            "};\n"
            "# engines the FDM actually has: FlightGear creates six engine\n"
            "# nodes for every aircraft, each with an empty running node; only\n"
            "# the real ones carry a value there\n"
            "var n = 0;\n"
            "for (var i = 0; i < 8; i += 1)\n"
            "    if (getprop(\"/engines/engine[\" ~ i ~ \"]/running\") != nil) n = i + 1;\n"
            "if (n < 1) n = 1;\n"
            "var fgtouch_generic = func {\n"
            "    setprop(\"/controls/fuel/tank[0]/fuel_selector\", 1);\n"
            "    setprop(\"/controls/fuel/tank[1]/fuel_selector\", 1);\n"
            "    setprop(\"/controls/switches/master-bat\", 1);\n"
            "    setprop(\"/controls/electric/battery-switch\", 1);\n"
            "    setprop(\"/controls/switches/master-alt\", 1);\n"
            "    setprop(\"/controls/switches/master-avionics\", 1);\n"
            "    setprop(\"/controls/switches/magnetos\", 3);\n"
            "    for (var i = 0; i < n; i += 1) {\n"
            "        var e = \"/controls/engines/engine[\" ~ i ~ \"]/\";\n"
            "        setprop(e ~ \"magnetos\", 3);\n"
            "        setprop(e ~ \"mixture\", 1.0);\n"
            "        setprop(e ~ \"primer\", 5);\n"
            "        setprop(e ~ \"throttle\", 0.25);\n"
            "        setprop(e ~ \"cutoff\", 0);\n"
            "        setprop(e ~ \"starter\", 1);\n"
            "    }\n"
            "    setprop(\"/controls/engines/current-engine/mixture\", 1.0);\n"
            "    setprop(\"/controls/engines/current-engine/throttle\", 0.25);\n"
            "    setprop(\"/controls/switches/starter\", 1);\n"
            "    # starters held until each engine runs, up to 90 s: a JSBSim\n"
            "    # turbine aborts its start the moment the starter drops\n"
            "    var fgtouch_started = systime();\n"
            "    var fgtouch_check = func {\n"
            "        var pending = 0;\n"
            "        for (var i = 0; i < n; i += 1) {\n"
            "            if (getprop(\"/engines/engine[\" ~ i ~ \"]/running\"))\n"
            "                setprop(\"/controls/engines/engine[\" ~ i ~ \"]/starter\", 0);\n"
            "            else\n"
            "                pending += 1;\n"
            "        }\n"
            "        if (pending > 0 and systime() - fgtouch_started < 90) {\n"
            "            settimer(fgtouch_check, 2);\n"
            "        } else {\n"
            "            for (var i = 0; i < n; i += 1)\n"
            "                setprop(\"/controls/engines/engine[\" ~ i ~ \"]/starter\", 0);\n"
            "            setprop(\"/controls/switches/starter\", 0);\n"
            "        }\n"
            "    };\n"
            "    settimer(fgtouch_check, 6);\n"
            "};\n"
            "# helicopters: a rotor brake left on keeps the rotor at a standstill\n"
            "# whatever the engines do\n"
            "setprop(\"/controls/rotor/brake\", 0);\n"
            "setprop(\"/controls/gear/brake-parking\", 0);\n"
            "if (contains(globals, \"acconfig\") and contains(acconfig, \"taxi\")) {\n"
            "    # A320 family: its configuration state 'ready for taxi' runs APU,\n"
            "    # bleed air and both engines in the order the FADEC wants\n"
            "    fgtouch_generic();\n"
            "    acconfig.taxi();\n"
            "} else {\n"
            "    var own = fgtouch_own([\"autostart\", \"startup\"]);\n"
            "    if (own == \"\") fgtouch_generic();\n"
            "    setprop(\"/sim/fgtouch/start-procedure\", own == \"\" ? \"generic\" : own);\n"
            "}\n"));

        _cranking = true;
        _throttle = 0.25;
        emit changed();

        /* The button reads "cranking" for ten seconds; the starters
           themselves are released by the script above, per engine. */
        QTimer::singleShot(10000, this, [this]{
            _cranking = false;
            _engineOn = true;
            emit changed();
        });
    }

    void stopEngine()
    {
        sendNasal(QByteArray(
            "var core = {tutorial:1, local_weather:1, jetways:1, jetways_edit:1, FailureMgr:1,\n"
            "            canvas:1, console:1, debug:1, input_helpers:1, performance_monitor:1,\n"
            "            std:1, towing:1, Autopush:1, modules:1};\n"
            "var done = 0;\n"
            "var nasalN = props.globals.getNode(\"/nasal\");\n"
            "if (nasalN != nil)\n"
            "    foreach (var c; nasalN.getChildren()) {\n"
            "        var ns = c.getName();\n"
            "        if (done or contains(core, ns) or !contains(globals, ns) or typeof(globals[ns]) != \"hash\") continue;\n"
            "        foreach (var fn; [\"autoshutdown\", \"shutdown\"])\n"
            "            if (!done and contains(globals[ns], fn) and typeof(globals[ns][fn]) == \"func\") {\n"
            "                var err = [];\n"
            "                call(globals[ns][fn], [], nil, nil, err);\n"
            "                done = size(err) == 0;\n"
            "            }\n"
            "    }\n"
            "var n = 0;\n"
            "for (var i = 0; i < 8; i += 1)\n"
            "    if (getprop(\"/engines/engine[\" ~ i ~ \"]/running\") != nil) n = i + 1;\n"
            "if (n < 1) n = 1;\n"
            "for (var i = 0; i < n; i += 1) {\n"
            "    var e = \"/controls/engines/engine[\" ~ i ~ \"]/\";\n"
            "    setprop(e ~ \"starter\", 0);\n"
            "    setprop(e ~ \"mixture\", 0.0);\n"
            "    setprop(e ~ \"magnetos\", 0);\n"
            "    setprop(e ~ \"cutoff\", 1);\n"
            "}\n"
            "setprop(\"/controls/engines/current-engine/mixture\", 0.0);\n"
            "setprop(\"/controls/switches/magnetos\", 0);\n"
            "setprop(\"/controls/switches/starter\", 0);\n"));
        _cranking = false;
        _engineOn = false;
        emit changed();
    }

    /* Reverse thrust, from the throttle held below zero.  Switched on
       entering that zone and off leaving it or letting go, both at idle.

       Most aircraft get the standard per-engine reverser property that
       JSBSim and YASim read, and the depth of the zone is the throttle.

       The A320 family has its own reverser logic, used through its toggle:
       it deploys only with both levers at idle AND its FADEC reporting
       IDLE, which happens a moment after the lever is back - the toggle
       sent together with throttle 0 was refused every time (BEFUNDE P52).
       So the script tries again for four seconds.  Once out, the FADEC
       takes the reverse thrust from throttle-rev and ignores the forward
       lever, so the depth goes there (0.05 idle reverse to 0.65 full, the
       range of its own keys) and the forward throttle stays at idle.
       Which of the two applies is only known inside the simulator: the
       script says so in /sim/fgtouch/reverse-mode, and the depth waits
       until that has been read back. */
    void setReverse(bool on)
    {
        if (on == _reversing) return;
        _reversing = on;
        _reverseDepth = 0;
        _throttle = 0;
        sendNasal(QByteArray(
            "var on = ") + (on ? "1" : "0") + QByteArray(";\n"
            "var own = contains(globals, \"systems\") and contains(systems, \"toggleFastRevThrust\");\n"
            "setprop(\"/sim/fgtouch/reverse-mode\", own ? \"aircraft\" : \"generic\");\n"
            "setprop(\"/sim/fgtouch/reverse-wanted\", on);\n"
            "if (own) {\n"
            "    if (on) setprop(\"/sim/fgtouch/reverse-depth\", 0);\n"
            "    var tries = 0;\n"
            "    var step = func {\n"
            "        if (getprop(\"/sim/fgtouch/reverse-wanted\") != on) return;\n"
            "        var now = getprop(\"/controls/engines/engine[0]/reverser\") ? 1 : 0;\n"
            "        if (now != on) {\n"
            "            systems.toggleFastRevThrust();\n"
            "            tries += 1;\n"
            "            if (tries < 16) settimer(step, 0.25);\n"
            "            return;\n"
            "        }\n"
            "        if (!on) return;\n"
            "        var d = getprop(\"/sim/fgtouch/reverse-depth\") or 0;\n"
            "        for (var i = 0; i < 8; i += 1)\n"
            "            if (getprop(\"/controls/engines/engine[\" ~ i ~ \"]/reverser\"))\n"
            "                setprop(\"/controls/engines/engine[\" ~ i ~ \"]/throttle-rev\", 0.05 + 0.60 * d);\n"
            "        settimer(step, 0.1);\n"
            "    };\n"
            "    step();\n"
            "} else {\n"
            "    setprop(\"/controls/engines/reverser-all\", on);\n"
            "    for (var i = 0; i < 8; i += 1)\n"
            "        setprop(\"/controls/engines/engine[\" ~ i ~ \"]/reverser\", on);\n"
            "}\n"));
        if (on) {
            _reverseMode = ReverseUnknown;
            queryReverseMode(0);
        }
        emit changed();
    }

    /* How much reverse thrust, 0..1, while reversing */
    void setReverseDepth(qreal depth)
    {
        _reverseDepth = clamp01(depth);
        if (!_reversing) return;
        if (_reverseMode == ReverseGeneric) {
            _throttle = _reverseDepth;
        } else if (_reverseMode == ReverseAircraft) {
            _throttle = 0;
            sendTelnet(QStringList() << "set /sim/fgtouch/reverse-depth "
                                        + QString::number(_reverseDepth, 'f', 2));
        }
        emit changed();
    }

    /* A start in the air or on final approach wants running engines.
       JSBSim starts them with /sim/presets/running, YASim jets and turbines
       run anyway, a YASim piston engine does not - so once the scenery is
       loaded, look, and start them where they are not running.  Never on
       running engines: the A320's start procedure begins with cold and
       dark, which in flight is a failure of everything. */
    /* throttle > 0: set that once flying (final approach - the trim for a
       descending path fails on many aircraft and leaves the lever at 0.8);
       0: adopt what the trim found (level flight, where it succeeds). */
    void startEngineWhenLoaded(qreal throttle)
    {
        _flightThrottle = throttle;
        _loadWaitStarted.start();
        pollLoadedForEngineStart();
    }

    void pollLoadedForEngineStart()
    {
        telnetQuery("get /sim/sceneryloaded", [this](const QString& out){
            if (!out.contains("'true'")) {
                if (_loadWaitStarted.elapsed() < 300000)
                    QTimer::singleShot(2000, this, &ControlSender::pollLoadedForEngineStart);
                return;
            }
            /* the flight model settles a few seconds after the scenery */
            QTimer::singleShot(4000, this, [this]{
                telnetQuery("get /engines/engine[0]/running", [this](const QString& out){
                    if (out.contains("'true'") || out.contains("'1'")) {
                        /* Take over the throttle the trim found.  The
                           sender writes the throttle 30 times a second, and
                           left at zero it would idle the engines the moment
                           the flight began. */
                        if (_flightThrottle > 0) {
                            _throttle = clamp01(_flightThrottle);
                            _engineOn = true;
                            emit changed();
                            return;
                        }
                        telnetQuery("get /controls/engines/engine[0]/throttle", [this](const QString& t){
                            const auto m = QRegularExpression("'([0-9.eE+-]+)'").match(t);
                            if (m.hasMatch()) _throttle = clamp01(m.captured(1).toDouble());
                            _engineOn = true;
                            emit changed();
                        });
                        return;
                    }
                    startEngine();
                    if (_flightThrottle > 0) {
                        _throttle = clamp01(_flightThrottle);
                        emit changed();
                    }
                });
            });
        });
    }

    /* Freeze the simulation - used when the application goes to the
       background (settings switch).  /sim/freeze/master stops the flight
       model and the clock; the frame rate is throttled hard as well, since
       FlightGear keeps drawing while frozen and that is what costs the
       battery.  On resume the throttle goes back to what the settings
       chose (0 = uncapped). */
    void setPaused(bool paused, int resumeThrottleHz)
    {
        if (paused == _paused) return;
        _paused = paused;
        sendTelnet(QStringList()
                   << QString("set /sim/freeze/master %1").arg(paused ? "true" : "false")
                   << QString("set /sim/freeze/clock %1").arg(paused ? "true" : "false")
                   << QString("set /sim/frame-rate-throttle-hz %1")
                      .arg(paused ? 2 : resumeThrottleHz));
        emit changed();
    }

    /* ---- tutorials --------------------------------------------------
       FlightGear's tutorials are step-by-step lessons defined by the
       aircraft (the c172p has fourteen); Nasal/tutorial/tutorial.nas drives
       them and shows each instruction in a PUI window - which the GLES
       build does not have.  It also stores the instruction in
       /sim/tutorials/last-message, so the application reads it from there
       and shows it itself. */
    void refreshTutorials()
    {
        _tutorials.clear();
        telnetQuery("ls /sim/tutorials", [this](const QString& out){
            /* tutorial/, tutorial[1]/, ... one directory per lesson */
            QStringList dirs;
            for (const QString& line : out.split('\n')) {
                const QString l = line.trimmed();
                if (l.startsWith("tutorial") && l.endsWith('/'))
                    dirs << l.left(l.size() - 1);
            }
            for (const QString& d : dirs) {
                const QString path = "/sim/tutorials/" + d;
                telnetQuery("ls " + path, [this, path, d](const QString& out){
                    QVariantMap e;
                    e["name"] = propValue(out, "name");
                    e["description"] = propValue(out, "description").simplified();
                    e["index"] = d;
                    e["lat"] = 0.0; e["lon"] = 0.0;
                    if (e["name"].toString().isEmpty()) return;
                    /* where the lesson repositions the aircraft to, so the
                       scenery there can be fetched first */
                    telnetQuery("ls " + path + "/presets", [this, e](const QString& out) mutable {
                        e["lat"] = propValue(out, "latitude-deg").toDouble();
                        e["lon"] = propValue(out, "longitude-deg").toDouble();
                        e["airport"] = propValue(out, "airport-id");
                        _tutorials.append(e);
                        emit tutorialsChanged();
                    });
                });
            }
        });
    }

    void startTutorial(const QString& name)
    {
        _tutorialName = name;
        _tutorialMessage = tr("Starting lesson…");
        _tutorialRunning = true;
        emit tutorialChanged();
        /* The tutorial module is off by default (defaults.xml,
           /nasal/tutorial/enabled); FlightGear loads it when that flag is
           set, which is what its own tutorial dialog does.  io.load_nasal
           as a fallback should the listener not have fired yet. */
        sendNasal(QString("setprop(\"/nasal/tutorial/enabled\", 1);\n"
                          "if (!contains(globals, \"tutorial\")) {\n"
                          "    io.load_nasal(getprop(\"/sim/fg-root\") ~ \"/Nasal/tutorial/tutorial.nas\", \"tutorial\");\n"
                          /* The module keeps its property handles in a
                             listener on /nasal/<module>/loaded, which the
                             Nasal subsystem sets after loading a module the
                             usual way.  Loading it by hand has to set it,
                             or every handle stays nil and the first call
                             dies with "non-objects have no members". */
                          "    setprop(\"/nasal/tutorial/loaded\", 1);\n"
                          "}\n"
                          "setprop(\"/sim/tutorials/current-tutorial\", \"%1\");\n"
                          "tutorial.startTutorial();\n").arg(name).toUtf8());
        _tutorialPoll.start();
    }

    void stopTutorial()
    {
        sendNasal("if (contains(globals, \"tutorial\")) tutorial.stopTutorial();\n");
        _tutorialPoll.stop();
        _tutorialRunning = false;
        _tutorialMessage.clear();
        _tutorialName.clear();
        emit tutorialChanged();
    }

    /* Next view (cockpit, chase, tower, ...): FlightGear's own view-cycle
       command, over the same telnet channel the engine start uses. */
    void cycleView()
    {
        sendTelnet(QStringList() << "run view-cycle");
    }

    /* Where the view points, relative to the nose: the look-to-the-side
       buttons and the drag gesture both end up here.

       goal- rather than plain heading-offset/pitch-offset: FlightGear
       interpolates towards the goal, so the view swings round instead of
       snapping, and a quick tap does not jerk the picture.

       Positive heading is to the left - the offset is measured
       counterclockwise from the nose.  Positive pitch is up.

       Works in any view, cockpit or outside: it is the current view that
       carries the offsets. */
    void setViewOffsets(int headingDeg, int pitchDeg)
    {
        /* Only send what changed, and at most every 50 ms: a drag produces
           a touch update per frame, and each one is a telnet command the
           simulator has to parse between frames. */
        if (headingDeg == _viewHeading && pitchDeg == _viewPitch) return;
        _viewHeading = headingDeg;
        _viewPitch = pitchDeg;
        if (!_viewSent.isValid() || _viewSent.elapsed() >= 50) {
            flushViewOffsets();
        } else if (!_viewTimer.isActive()) {
            _viewTimer.start(50 - int(_viewSent.elapsed()));
        }
    }

    /* Field of view in degrees, from the pinch gesture. */
    void setFieldOfView(int deg)
    {
        sendTelnet(QStringList()
                   << QString("set /sim/current-view/field-of-view %1").arg(deg));
    }

    /* Aktuelle Lage als Nullpunkt uebernehmen - so kann man auch
       im Liegen fliegen. */
    void calibrate()
    {
        if (QAccelerometerReading* r = _accel.reading()) {
            _refX = r->x();
            _refY = r->y();
            _refZ = r->z();
            _haveRef = true;
        }
    }

private slots:
    void sendPacket()
    {
        updateFromSensors();

        char buf[256];
        /* One throttle field per engine, eight of them, then the lever:
           the engine models read engine[N], the cockpit lever's animation
           reads current-engine.  The field order is the protocol written
           in FgRuntime::start. */
        /* Not qsnprintf: under a German locale it wrote 0,2500 and the
           comma separated protocol read twice as many fields - the throttle
           got 9943, the lever field 2500.  QByteArray::number is always the
           C locale. */
        const QByteArray throttle = QByteArray::number(_throttle, 'f', 4);
        QByteArray throttles = throttle;
        for (int i = 1; i < 8; ++i) throttles += ',' + throttle;
        const QByteArray line = QByteArray::number(_aileron, 'f', 4) + ','
            + QByteArray::number(_elevator, 'f', 4) + ','
            + QByteArray::number(_rudder, 'f', 4) + ','
            + throttles + ','
            + QByteArray::number(_brake, 'f', 4) + ','
            + QByteArray::number(_flaps, 'f', 4) + ','
            + QByteArray::number(_gearDown ? 1 : 0) + ','
            + QByteArray::number(_throttle, 'f', 4) + '\n';
        const int n = qMin(int(line.size()), int(sizeof buf) - 1);
        memcpy(buf, line.constData(), n); buf[n] = 0;
        if (n > 0)
            _sock.writeDatagram(buf, n, QHostAddress::LocalHost, 5501);
    }

private:
    void updateFromSensors()
    {
        if (!_tiltActive) { _aileron = 0.0; _elevator = 0.0; return; }

        QAccelerometerReading* r = _accel.reading();
        if (!r) return;
        if (!_haveRef) { calibrate(); return; }

        /* Landscape: Geraet liegt quer.
           Rollen um die Laengsachse -> Querruder
           Kippen vor/zurueck        -> Hoehenruder */
        /* Angles, not components.  The old dx/G assumed the reference lay
           flat; held at 40 degrees, as one holds a phone to look at it,
           that left 20 percent of travel one way and 100 the other.  Thirty
           degrees of tilt either side of the reference is full deflection. */
        /* 18 degrees either side of the reference is full deflection.  Thirty
           was too little: seven degrees of tilt gave eight percent of
           elevator after dead zone and expo, and nothing happened. */
        const qreal MAX_ANGLE = 18.0 * M_PI / 180.0;
        const qreal pitchAngle = std::atan2(r->x(), r->z());
        const qreal rollAngle  = std::atan2(r->y(), std::sqrt(r->x() * r->x() + r->z() * r->z()));
        const qreal refPitch   = std::atan2(_refX, _refZ);
        const qreal refRoll    = std::atan2(_refY, std::sqrt(_refX * _refX + _refZ * _refZ));
        /* Rolling the phone is a wider, easier motion than pitching it, so
           the roll axis gets a range of its own - 35 degrees for full
           aileron against 18 for full elevator. */
        const qreal ROLL_ANGLE = 35.0 * M_PI / 180.0;
        qreal roll  = qBound(-1.0, (rollAngle  - refRoll)  / ROLL_ANGLE, 1.0);
        /* Tilting the top edge towards the pilot is pulling the yoke, which
           FlightGear wants as a negative elevator - hence the sign. */
        qreal pitch = qBound(-1.0, -(pitchAngle - refPitch) / MAX_ANGLE, 1.0);

        roll  = shape(roll);
        pitch = shape(pitch);

        /* Tiefpass gegen Handzittern */
        const qreal a = 0.25;
        _aileron  = _aileron  * (1.0 - a) + roll  * a;
        _elevator = _elevator * (1.0 - a) + pitch * a;

        emit changed();
    }

    /* Totzone plus Expo - ohne das ist Praezisionsflug unmoeglich */
    static qreal shape(qreal v)
    {
        const qreal dead = 0.03;
        if (qAbs(v) < dead) return 0.0;
        const qreal s = (qAbs(v) - dead) / (1.0 - dead);
        const qreal k = 0.35;
        const qreal e = k * s * s * s + (1.0 - k) * s;
        return v < 0 ? -e : e;
    }

    void flushViewOffsets()
    {
        _viewSent.start();
        sendTelnet(QStringList()
                   << QString("set /sim/current-view/goal-heading-offset-deg %1")
                      .arg(_viewHeading)
                   << QString("set /sim/current-view/goal-pitch-offset-deg %1")
                      .arg(_viewPitch));
    }

    /* One connection for all telnet traffic, kept open.

       It used to be a fresh connection per command set.  A fast drag
       opened dozens of them within a second, and FlightGear serves its
       telnet clients in whatever order they happen to be polled - so an
       older view offset could land after a newer one, and the camera
       jumped back and forth between two directions.  On a single
       connection the commands arrive in the order they were written.

       The replies (prompts, echoed values) are read and dropped, so the
       receive buffer never fills up and stalls the simulator's writes -
       that was the reason the old code did not keep a session. */
    void sendTelnet(const QStringList& cmds)
    {
        QByteArray b;
        for (const QString& c : cmds) b += (c + "\r\n").toUtf8();
        telnetWrite(b);
    }

    /* A Nasal script over the same channel: "nasal" switches the
       connection into script mode until the ##EOF## line.  Needs
       --allow-nasal-from-sockets on the simulator's command line, which
       FgRuntime passes. */
    void sendNasal(const QByteArray& script)
    {
        QByteArray b = "nasal\r\n" + script;
        if (!script.endsWith('\n')) b += "\n";
        b += "##EOF##\r\n";
        telnetWrite(b);
    }

    /* A second connection for questions.  One command in flight at a
       time: its reply ends with FlightGear's prompt "/> ", which is what
       tells the answer apart from the next one.  Kept apart from the
       command connection so its replies do not have to be counted
       against fire-and-forget commands and Nasal blocks. */
    void telnetQuery(const QString& cmd, std::function<void(const QString&)> cb)
    {
        _queries.enqueue(qMakePair(cmd, cb));
        pumpQueries();
    }

    void pumpQueries()
    {
        if (_queryBusy || _queries.isEmpty()) return;
        if (!_tnq) {
            _tnq = new QTcpSocket(this);
            connect(_tnq, &QTcpSocket::readyRead, this, &ControlSender::onQueryData);
            connect(_tnq, &QTcpSocket::connected, this, [this]{ _queryBusy = false; pumpQueries(); });
            auto gone = [this]{
                if (_tnq) { _tnq->deleteLater(); _tnq = nullptr; }
                _queryBusy = false;
                _queryRx.clear();
                /* the question in flight gets no answer; drop it so the
                   queue does not stall forever */
                if (!_queries.isEmpty() && _queryInFlight) _queries.dequeue();
                _queryInFlight = false;
            };
            connect(_tnq, &QTcpSocket::disconnected, this, gone);
            connect(_tnq, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
                    this, [gone](QAbstractSocket::SocketError){ gone(); });
            _tnq->connectToHost(QHostAddress::LocalHost, 5401);
            _queryBusy = true;          // until connected
            return;
        }
        if (_tnq->state() != QAbstractSocket::ConnectedState) return;
        _queryBusy = true;
        _queryInFlight = true;
        _queryRx.clear();
        _tnq->write((_queries.head().first + "\r\n").toUtf8());
    }

    void onQueryData()
    {
        _queryRx += _tnq->readAll();
        const int p = _queryRx.indexOf("/> ");
        if (p < 0) return;
        const QString reply = QString::fromUtf8(_queryRx.left(p));
        _queryRx = _queryRx.mid(p + 3);
        if (!_queries.isEmpty()) {
            auto q = _queries.dequeue();
            _queryInFlight = false;
            _queryBusy = false;
            q.second(reply);
        } else {
            _queryBusy = false;
        }
        pumpQueries();
    }

    /* Which reverser logic the loaded aircraft uses, as setReverse's
       script wrote it.  The script goes over the command connection and
       this question over the other one, so the answer may still be the
       empty property: ask again shortly. */
    void queryReverseMode(int attempt)
    {
        telnetQuery("get /sim/fgtouch/reverse-mode", [this, attempt](const QString& out){
            if (!_reversing) return;
            if (out.contains("'aircraft'")) _reverseMode = ReverseAircraft;
            else if (out.contains("'generic'")) _reverseMode = ReverseGeneric;
            else {
                if (attempt < 20)
                    QTimer::singleShot(150, this, [this, attempt]{ queryReverseMode(attempt + 1); });
                return;
            }
            setReverseDepth(_reverseDepth);
        });
    }

    /* "name = 'value' (type)" out of an ls reply */
    static QString propValue(const QString& lsOut, const QString& name)
    {
        const QRegularExpression re("(?:^|\\n)" + QRegularExpression::escape(name)
                                    + "\\s*=\\s*'(.*?)'\\s*\\(", QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(lsOut);
        return m.hasMatch() ? m.captured(1) : QString();
    }

    void pollTutorial()
    {
        telnetQuery("ls /sim/tutorials", [this](const QString& out){
            const QString msg = propValue(out, "last-message");
            const QString running = propValue(out, "running");
            bool changedNow = false;
            if (!msg.isEmpty() && msg != _tutorialMessage) { _tutorialMessage = msg; changedNow = true; }
            /* tutorial.nas writes 1 and 0, not true and false; an empty
               string means the property is not there yet, which is not the
               same as "ended" */
            if ((running == "0" || running == "false") && _tutorialRunning) {
                /* the lesson ended by itself (last step done) */
                _tutorialRunning = false;
                _tutorialPoll.stop();
                changedNow = true;
            }
            if (changedNow) emit tutorialChanged();
        });
    }

    void telnetWrite(const QByteArray& b)
    {
        if (!_telnet) {
            _telnet = new QTcpSocket(this);
            connect(_telnet, &QTcpSocket::readyRead, _telnet, [this]{ _telnet->readAll(); });
            connect(_telnet, &QTcpSocket::connected, this, [this]{
                if (!_telnetQueue.isEmpty()) { _telnet->write(_telnetQueue); _telnetQueue.clear(); }
            });
            /* Gone (simulator stopped, refused while it is still
               starting): drop the socket, the next command reconnects.
               What was queued is kept for that attempt. */
            auto gone = [this]{
                if (_telnet) { _telnet->deleteLater(); _telnet = nullptr; }
            };
            connect(_telnet, &QTcpSocket::disconnected, this, gone);
            connect(_telnet, static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
                    this, [gone](QAbstractSocket::SocketError){ gone(); });
            _telnet->connectToHost(QHostAddress::LocalHost, 5401);
        }
        if (_telnet->state() == QAbstractSocket::ConnectedState) {
            _telnet->write(b);
        } else {
            _telnetQueue += b;
            if (_telnetQueue.size() > 65536) _telnetQueue = _telnetQueue.right(65536);
        }
    }

    static qreal clamp01(qreal v) { return qBound(0.0, v, 1.0); }
    static qreal clamp11(qreal v) { return qBound(-1.0, v, 1.0); }

signals:
    void changed();
    void tutorialsChanged();
    void tutorialChanged();

private:
    QUdpSocket _sock;
    QAccelerometer _accel;
    QTimer _tick;

    qreal _throttle = 0.0, _rudder = 0.0, _flaps = 0.0, _brake = 0.0;
    qreal _aileron = 0.0, _elevator = 0.0;
    bool  _gearDown = true;
    bool  _tiltActive = true;   /* first reading calibrates the reference */
    bool  _cranking = false;
    bool  _engineOn = false;
    QTcpSocket* _telnet = nullptr;
    QByteArray  _telnetQueue;
    QTcpSocket* _tnq = nullptr;
    QByteArray  _queryRx;
    QQueue<QPair<QString, std::function<void(const QString&)>>> _queries;
    bool _queryBusy = false, _queryInFlight = false;
    QVariantList _tutorials;
    bool    _tutorialRunning = false;
    QString _tutorialMessage, _tutorialName;
    QTimer  _tutorialPoll;
    bool    _paused = false;
    bool    _reversing = false;
    qreal   _reverseDepth = 0.0;
    enum { ReverseUnknown, ReverseGeneric, ReverseAircraft } _reverseMode = ReverseUnknown;
    QElapsedTimer _loadWaitStarted;
    qreal _flightThrottle = 0.0;
    QElapsedTimer _viewSent;
    QTimer _viewTimer;
    /* Last view offsets sent, so a drag only puts a command on the wire
       when the rounded degree actually changes. */
    int   _viewHeading = 0;
    int   _viewPitch = 0;

    qreal _refX = 0, _refY = 0, _refZ = 0;
    bool  _haveRef = false;
};


// ===================================================================

int main(int argc, char* argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    qmlRegisterType<FrameItem>("harbour.fgview", 1, 0, "FrameItem");
    qmlRegisterType<ControlSender>("harbour.fgview", 1, 0, "ControlSender");
    qmlRegisterType<FgRuntime>("harbour.fgview", 1, 0, "FgRuntime");

    /* Beim Beenden alle Kindprozesse abraeumen - im Destruktor ist
       es zu spaet, dann hat Qt die QProcess-Objekte schon zerlegt. */
    /* Kein pkill hier - das reisst QProcess den Prozess unter den
       Fuessen weg. FgRuntime raeumt seine Kinder selbst ab. */

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->setSource(SailfishApp::pathTo("qml/harbour-fgview.qml"));
    view->show();

    return app->exec();
}

#include "harbour-fgview.moc"
