/* fgruntime.h - Erststart-Download und Prozessverwaltung fuer harbour-fgview
 *
 * In harbour-fgview.cpp einbinden:
 *     #include "fgruntime.h"
 * und in main() registrieren:
 *     qmlRegisterType<FgRuntime>("harbour.fgview", 1, 0, "FgRuntime");
 *
 * Ablauf:
 *   1. dataReady prueft, ob FGData bereits entpackt vorliegt
 *   2. downloadData() holt das Archiv mit aria2c (8 Verbindungen,
 *      fortsetzbar) und entpackt es
 *   3. startSim() startet fgfs mit der Zink-Umgebung
 */

#ifndef FGRUNTIME_H
#define FGRUNTIME_H

#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDirIterator>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

class FgRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    dataReady   READ dataReady   NOTIFY stateChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY stateChanged)
    Q_PROPERTY(bool    simRunning  READ simRunning  NOTIFY stateChanged)
    Q_PROPERTY(int     progress    READ progress    NOTIFY progressChanged)
    Q_PROPERTY(QString status      READ status      NOTIFY progressChanged)
    Q_PROPERTY(QString speed       READ speed       NOTIFY progressChanged)

public:
    explicit FgRuntime(QObject* parent = nullptr) : QObject(parent)
    {
        _root = QStandardPaths::writableLocation(
                    QStandardPaths::GenericDataLocation)
                + "/harbour-fgview";
        QDir().mkpath(_root);

        connect(&_dl, &QProcess::readyReadStandardOutput,
                this, &FgRuntime::onDownloadOutput);
        connect(&_dl, &QProcess::readyReadStandardError,
                this, &FgRuntime::onDownloadOutput);
        _extractTick.setInterval(2000);
        connect(&_extractTick, &QTimer::timeout,
                this, &FgRuntime::updateExtractProgress);

        _heartbeat.setInterval(1000);
        connect(&_heartbeat, &QTimer::timeout, this, [this]{
            if (busy()) { emit stateChanged(); emit progressChanged(); }
            else _heartbeat.stop();
        });
        connect(&_dl, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onDownloadFinished);

        connect(&_tar, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onExtractFinished);

        connect(&_sim, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus){
                    _status = tr("Simulator stopped");
                    emit stateChanged();
                    emit progressChanged();
                });
    }

    bool dataReady() const
    {
        return QFileInfo::exists(fgRoot() + "/version");
    }
    bool busy() const
    {
        return _dl.state() != QProcess::NotRunning
            || _tar.state() != QProcess::NotRunning;
    }
    bool simRunning() const { return _sim.state() != QProcess::NotRunning; }
    int progress() const { return _progress; }
    QString status() const { return _status; }
    QString speed() const { return _speed; }

    QString fgRoot() const { return _root + "/fgdata"; }

public slots:
    void downloadData()
    {
        if (busy() || _resolving) return;

        /* Archiv schon vollstaendig? Dann direkt entpacken. */
        const QString archive = _root + "/fgdata.txz";
        if (QFileInfo(archive).size() == ARCHIVE_BYTES
            && !QFileInfo::exists(archive + ".aria2")) {
            _progress = 100;
            _speed.clear();
            emit progressChanged();
            extractArchive();
            return;
        }

        _status = tr("Looking up mirror…");
        _progress = 0;
        emit progressChanged();
        emit stateChanged();

        /* Qt 5.6 auf SailfishOS hat kein nutzbares SSL-Backend
           (gebaut gegen OpenSSL 1.0, System hat 1.1/3.x), deshalb
           loesen wir die Weiterleitung mit curl auf. */
        _resolving = true;
        _resolve.setProcessChannelMode(QProcess::MergedChannels);
        _resolve.start("curl", QStringList()
                       << "-sIL"
                       << "https://sourceforge.net/projects/flightgear/files/"
                          "release-2020.3/FlightGear-2020.3.19-data.txz/download");
        if (!_resolve.waitForFinished(20000)) {
            _resolve.kill();
            _resolving = false;
            _status = tr("Mirror lookup timed out");
            emit progressChanged();
            emit stateChanged();
            return;
        }
        _resolving = false;

        QString url;
        const QStringList lines =
            QString::fromUtf8(_resolve.readAll()).split('\n');
        for (const QString& l : lines) {
            if (l.startsWith("location:", Qt::CaseInsensitive))
                url = l.mid(9).trimmed();
        }
        if (url.isEmpty()) {
            _status = tr("Could not determine the mirror address");
            emit progressChanged();
            emit stateChanged();
            return;
        }
        qWarning("FGVIEW resolved URL: %s", qPrintable(url));
        startAria(url);
    }

    void startAria(const QString& url)
    {
        _status = tr("Downloading base data (about 1.7 GB)…");
        emit progressChanged();
        emit stateChanged();

        _dl.setProcessChannelMode(QProcess::MergedChannels);
        _dl.start("aria2c", QStringList()
                  << "-x" << "8"
                  << "-s" << "8"
                  << "-k" << "4M"
                  << "-c"
                  << "--summary-interval=1"
                  << "--console-log-level=warn"
                  << "--auto-file-renaming=false"
                  << "--allow-piece-length-change=true"
                  << "--user-agent=Mozilla/5.0"
                  << "--check-certificate=false"
                  << "--always-resume=true"
                  << "--max-tries=5"
                  << "--retry-wait=3"
                  << "-d" << _root
                  << "-o" << "fgdata.txz"
                  << url);
        if (!_dl.waitForStarted(3000)) {
            _status = tr("aria2c could not be started. "
                         "Is the aria2 package installed?");
            emit progressChanged();
            emit stateChanged();
        }
    }

    void cancelDownload()
    {
        if (_dl.state() != QProcess::NotRunning) _dl.terminate();
        if (_tar.state() != QProcess::NotRunning) _tar.terminate();
        _status = tr("Cancelled");
        emit progressChanged();
        emit stateChanged();
    }

    void startSim(const QString& aircraft = "c172p",
                  const QString& airport  = "LOWW")
    {
        if (simRunning() || !dataReady()) return;

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LD_LIBRARY_PATH", "/opt/mesa-zink/lib64:/opt/fgfs/lib");
        env.insert("__EGL_VENDOR_LIBRARY_DIRS",
                   "/opt/mesa-zink/share/glvnd/egl_vendor.d");
        env.insert("EGL_PLATFORM", "wayland");
        env.insert("MESA_LOADER_DRIVER_OVERRIDE", "zink");
        env.insert("XDG_RUNTIME_DIR", "/run/display");
        env.insert("WAYLAND_DISPLAY", "wayland-0");
        env.insert("FGFS_SHM", "1");
        env.remove("GALLIUM_DRIVER");

        _sim.setProcessEnvironment(env);
        _sim.setProcessChannelMode(QProcess::ForwardedChannels);
        _sim.start("/opt/fgfs/bin/fgfs", QStringList()
                   << "--fg-root=" + fgRoot()
                   << "--disable-terrasync"
                   << "--disable-ai-models"
                   << "--disable-real-weather-fetch"
                   << "--disable-sound"
                   << "--prop:/sim/rendering/shadows/enabled=false"
                   << "--prop:/sim/rendering/multi-sample-buffers=0"
                   << "--generic=socket,in,30,,5501,udp,fgtouch"
                   << "--aircraft=" + aircraft
                   << "--airport=" + airport
                   << "--timeofday=noon");

        _status = tr("Simulator starting — this takes a minute or two");
        emit stateChanged();
        emit progressChanged();
    }

    void stopSim()
    {
        if (_sim.state() != QProcess::NotRunning) {
            _sim.terminate();
            if (!_sim.waitForFinished(3000)) _sim.kill();
        }
        emit stateChanged();
    }

private slots:
    void onDownloadOutput()
    {
        const QString out = QString::fromUtf8(_dl.readAllStandardOutput())
                         + QString::fromUtf8(_dl.readAllStandardError());

        /* aria2 meldet z.B.:
           [#4926fa 1.0GiB/1.6GiB(60%) CN:8 DL:2.4MiB ETA:4m35s] */
        static const QRegularExpression rePct("\\((\\d+)%\\)");
        static const QRegularExpression reDl("DL:([0-9.]+[KMG]i?B)");

        auto m = rePct.match(out);
        if (m.hasMatch()) {
            _progress = m.captured(1).toInt();
        }
        static const QRegularExpression reEta("ETA:([0-9dhms]+)");

        auto d = reDl.match(out);
        auto e = reEta.match(out);
        if (d.hasMatch()) {
            _speed = d.captured(1) + "/s";
            if (e.hasMatch())
                _speed += "  ETA " + e.captured(1);
        }
        if (m.hasMatch() || d.hasMatch()) emit progressChanged();
    }

    void onDownloadFinished(int code, QProcess::ExitStatus)
    {
        if (code != 0) {
            _status = tr("Download failed (code %1)").arg(code);
            emit progressChanged();
            emit stateChanged();
            return;
        }

        extractArchive();
    }

    void extractArchive()
    {
        _status = tr("Extracting base data…");
        _progress = 0;
        _speed.clear();
        emit progressChanged();
        emit stateChanged();

        QDir().mkpath(fgRoot());
        /* Das Archiv enthaelt ein Verzeichnis "fgdata" - eine Ebene
           abschneiden, damit es direkt in fgRoot landet. */
        _tar.start("tar", QStringList()
                   << "xf" << _root + "/fgdata.txz"
                   << "-C" << fgRoot()
                   << "--strip-components=1");
        _tar.waitForStarted(3000);
        _extractTick.start();
        emit stateChanged();
    }

    /* Fortschritt beim Entpacken aus der wachsenden Verzeichnisgroesse.
       Vollstaendig entpackt sind es rund 2,7 GB. */
    void updateExtractProgress()
    {
        if (_tar.state() == QProcess::NotRunning) {
            _extractTick.stop();
            return;
        }
        quint64 bytes = 0;
        QDirIterator it(fgRoot(), QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        int guard = 0;
        while (it.hasNext() && ++guard < 40000) {
            it.next();
            bytes += quint64(it.fileInfo().size());
        }
        _progress = int(qMin<quint64>(99, bytes * 100 / EXTRACTED_BYTES));
        _speed = QString("%1 / %2 GB")
                 .arg(bytes / 1.0e9, 0, 'f', 1)
                 .arg(EXTRACTED_BYTES / 1.0e9, 0, 'f', 1);
        emit progressChanged();
    }

    void onExtractFinished(int code, QProcess::ExitStatus)
    {
        if (code == 0 && dataReady()) {
            QDir().mkpath(fgRoot() + "/Protocol");
            QFile::remove(fgRoot() + "/Protocol/fgtouch.xml");
            QFile::copy("/opt/fgfs/share/fgtouch.xml",
                        fgRoot() + "/Protocol/fgtouch.xml");
            _status = tr("Base data ready");
            QFile::remove(_root + "/fgdata.txz");
            QFile::remove(_root + "/fgdata.txz.aria2");
        } else {
            _status = tr("Extraction failed (code %1)").arg(code);
        }
        emit progressChanged();
        emit stateChanged();
    }

signals:
    void stateChanged();
    void progressChanged();

private:
    QString _root;
    static const qint64  ARCHIVE_BYTES   = 1789370768LL;
    static const quint64 EXTRACTED_BYTES = 2705459200ULL;

    QProcess _dl, _tar, _sim, _resolve;
    QTimer _heartbeat;
    QTimer _extractTick;
    QNetworkAccessManager _nam;
    bool _resolving = false;
    int _progress = 0;
    QString _status;
    QString _speed;
};

#endif // FGRUNTIME_H
