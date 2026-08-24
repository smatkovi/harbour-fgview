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
#include <QTimer>
#include <QAccelerometer>
#include <QGyroscope>
#include <QtMath>

#include <sailfishapp.h>
#include "fgruntime.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>

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
};
#pragma pack(pop)

static const quint32 FGFR_MAGIC = 0x46474652u;
static const int     FGFR_HDR   = 32;

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
    }

    ~FrameItem() override { closeShm(); }

    bool connected() const { return _base != nullptr; }
    int  fps() const { return _fps; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override
    {
        if (_image.isNull()) { delete old; return nullptr; }

        QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(old);
        if (!node) {
            node = new QSGSimpleTextureNode();
            node->setFiltering(QSGTexture::Linear);
        }

        if (_texture) { delete _texture; _texture = nullptr; }
        _texture = window()->createTextureFromImage(_image);
        node->setTexture(_texture);

        // Seitenverhaeltnis wahren, zentriert einpassen
        const qreal iw = _image.width(), ih = _image.height();
        const qreal sw = width(), sh = height();
        if (iw <= 0 || ih <= 0 || sw <= 0 || sh <= 0) return node;

        const qreal scale = qMin(sw / iw, sh / ih);
        const qreal dw = iw * scale, dh = ih * scale;
        node->setRect((sw - dw) / 2.0, (sh - dh) / 2.0, dw, dh);
        return node;
    }

private slots:
    void poll()
    {
        if (!_base && !openShm()) return;

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

        const uchar* src = _base + FGFR_HDR + size_t(slot) * bytes;

        /* GL liefert von unten nach oben - zeilenweise gespiegelt kopieren */
        const int stride = w * 4;
        for (int y = 0; y < h; ++y)
            memcpy(_image.scanLine(h - 1 - y), src + size_t(y) * stride, stride);

        const quint64 s2 = hdr->sequence;
        if (s2 != s1) return;                   // Frame war inkonsistent

        _lastSeq = s1;
        ++_framesSinceTick;
        update();
    }

private:
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
        if (_base) { ::munmap(_base, _mapped); _base = nullptr; }
        if (_texture) { delete _texture; _texture = nullptr; }
    }

signals:
    void connectedChanged();
    void fpsChanged();

private:
    uchar*  _base = nullptr;
    size_t  _mapped = 0;
    quint64 _lastSeq = 0;
    QImage  _image;
    QSGTexture* _texture = nullptr;
    QTimer  _poll, _fpsTimer;
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

public:
    explicit ControlSender(QObject* parent = nullptr) : QObject(parent)
    {
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

    void setThrottle(qreal v) { _throttle = clamp01(v); emit changed(); }
    void setRudder(qreal v)   { _rudder = clamp11(v);   emit changed(); }
    void setFlaps(qreal v)    { _flaps = clamp01(v);    emit changed(); }
    void setBrake(qreal v)    { _brake = clamp01(v);    emit changed(); }
    void setGearDown(bool v)  { _gearDown = v;          emit changed(); }
    void setTiltActive(bool v){ _tiltActive = v;        emit changed(); }

public slots:
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

        char buf[160];
        const int n = qsnprintf(buf, sizeof buf,
                                "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
                                _aileron, _elevator, _rudder,
                                _throttle, _brake, _flaps,
                                _gearDown ? 1 : 0);
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
        const qreal dx = r->x() - _refX;
        const qreal dy = r->y() - _refY;

        const qreal G = 9.81;
        qreal roll  = qBound(-1.0, dy / G, 1.0);
        qreal pitch = qBound(-1.0, dx / G, 1.0);

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
        const qreal dead = 0.06;
        if (qAbs(v) < dead) return 0.0;
        const qreal s = (qAbs(v) - dead) / (1.0 - dead);
        const qreal k = 0.6;
        const qreal e = k * s * s * s + (1.0 - k) * s;
        return v < 0 ? -e : e;
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
    bool  _tiltActive = false;

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

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->setSource(SailfishApp::pathTo("qml/harbour-fgview.qml"));
    view->show();

    return app->exec();
}

#include "harbour-fgview.moc"
