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

        sendNasal(
            "# engines the FDM actually has: FlightGear creates six engine\n"
            "# nodes for every aircraft, each with an empty running node; only\n"
            "# the real ones carry a value there\n"
            "var n = 0;\n"
            "for (var i = 0; i < 8; i += 1)\n"
            "    if (getprop(\"/engines/engine[\" ~ i ~ \"]/running\") != nil) n = i + 1;\n"
            "if (n < 1) n = 1;\n"
            "setprop(\"/controls/fuel/tank[0]/fuel_selector\", 1);\n"
            "setprop(\"/controls/fuel/tank[1]/fuel_selector\", 1);\n"
            "setprop(\"/controls/switches/master-bat\", 1);\n"
            "setprop(\"/controls/electric/battery-switch\", 1);\n"
            "setprop(\"/controls/switches/master-alt\", 1);\n"
            "setprop(\"/controls/switches/master-avionics\", 1);\n"
            "setprop(\"/controls/switches/magnetos\", 3);\n"
            "setprop(\"/controls/gear/brake-parking\", 0);\n"
            "for (var i = 0; i < n; i += 1) {\n"
            "    var e = \"/controls/engines/engine[\" ~ i ~ \"]/\";\n"
            "    setprop(e ~ \"magnetos\", 3);\n"
            "    setprop(e ~ \"mixture\", 1.0);\n"
            "    setprop(e ~ \"primer\", 5);\n"
            "    setprop(e ~ \"throttle\", 0.25);\n"
            "    setprop(e ~ \"cutoff\", 0);\n"
            "    setprop(e ~ \"starter\", 1);\n"
            "}\n"
            "setprop(\"/controls/engines/current-engine/mixture\", 1.0);\n"
            "setprop(\"/controls/engines/current-engine/throttle\", 0.25);\n"
            "setprop(\"/controls/switches/starter\", 1);\n"
            "var fgtouch_started = systime();\n"
            "var fgtouch_check = func {\n"
            "    var pending = 0;\n"
            "    for (var i = 0; i < n; i += 1) {\n"
            "        if (getprop(\"/engines/engine[\" ~ i ~ \"]/running\"))\n"
            "            setprop(\"/controls/engines/engine[\" ~ i ~ \"]/starter\", 0);\n"
            "        else\n"
            "            pending += 1;\n"
            "    }\n"
            "    if (pending > 0 and systime() - fgtouch_started < 90) {\n"
            "        settimer(fgtouch_check, 2);\n"
            "    } else {\n"
            "        for (var i = 0; i < n; i += 1)\n"
            "            setprop(\"/controls/engines/engine[\" ~ i ~ \"]/starter\", 0);\n"
            "        setprop(\"/controls/switches/starter\", 0);\n"
            "    }\n"
            "};\n"
            "settimer(fgtouch_check, 6);\n"
            "if (contains(globals, \"acconfig\") and contains(acconfig, \"taxi\")) acconfig.taxi();\n");

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
        sendNasal(
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
            "setprop(\"/controls/switches/starter\", 0);\n");
        _cranking = false;
        _engineOn = false;
        emit changed();
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
