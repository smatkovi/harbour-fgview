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
#include <unistd.h>
#include <QTimer>
#include <QDirIterator>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <cmath>

class FgRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    dataReady   READ dataReady   NOTIFY stateChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY stateChanged)
    Q_PROPERTY(bool    simRunning  READ simRunning  NOTIFY stateChanged)
    Q_PROPERTY(bool    sceneryBusy READ sceneryBusy NOTIFY stateChanged)
    Q_PROPERTY(int     progress    READ progress    NOTIFY progressChanged)
    Q_PROPERTY(QString status      READ status      NOTIFY progressChanged)
    Q_PROPERTY(QString speed       READ speed       NOTIFY progressChanged)
    Q_PROPERTY(QString simLog      READ simLog      NOTIFY simLogChanged)
    Q_PROPERTY(QVariantList aircraft     READ aircraft     NOTIFY aircraftChanged)
    /* FlightGear's AI scenarios (carriers, tankers, wingmen, ...) */
    Q_PROPERTY(QVariantList scenarios    READ scenarios    NOTIFY aircraftChanged)
    Q_PROPERTY(QVariantList catalog      READ catalog      NOTIFY catalogChanged)
    Q_PROPERTY(bool         catalogBusy  READ catalogBusy  NOTIFY catalogChanged)
    Q_PROPERTY(QString      hangarStatus READ hangarStatus NOTIFY catalogChanged)
    Q_PROPERTY(int          hangarProgress READ hangarProgress NOTIFY catalogChanged)

public:
    ~FgRuntime() override
    {
        /* Zuerst die Signale trennen, sonst laufen Slots waehrend des
           Abbaus in bereits zerlegte Objekte. */
        _heartbeat.stop();
        _extractTick.stop();
        disconnect(&_dl,  nullptr, this, nullptr);
        disconnect(&_tar, nullptr, this, nullptr);
        disconnect(&_sim, nullptr, this, nullptr);

        /* aria2 und fgfs laufen nur solange die App offen ist */
        QProcess* procs[] = { &_dl, &_tar, &_sim };
        for (QProcess* p : procs) {
            if (p->state() != QProcess::NotRunning) {
                p->terminate();
                if (!p->waitForFinished(3000)) p->kill();
                p->waitForFinished(1000);
            }
        }
        if (_logFile.isOpen()) _logFile.close();
    }

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
        connect(&_sim, &QProcess::readyReadStandardOutput,
                this, &FgRuntime::onSimOutput);
        connect(&_scenery, &QProcess::readyReadStandardOutput,
                this, &FgRuntime::onSceneryOutput);
        connect(&_scenery, &QProcess::readyReadStandardError,
                this, &FgRuntime::onSceneryOutput);
        connect(&_scenery,
                static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onSceneryFinished);

        refreshAircraft();
        refreshScenarios();

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

        connect(&_cat, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onCatalogFinished);
        connect(&_acDl, &QProcess::readyReadStandardOutput,
                this, &FgRuntime::onAircraftOutput);
        connect(&_acDl, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onAircraftDownloaded);
        connect(&_acUnzip, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
                this, &FgRuntime::onAircraftUnpacked);

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
    /* The scenery phase counts as running: the start button has to become
       "stop" while the tiles are coming in, and a second start must not
       slip past. */
    bool simRunning() const
    {
        return _sim.state() != QProcess::NotRunning
            || (_scenery.state() != QProcess::NotRunning && _sceneryThen == SceneryThenLaunch);
    }
    bool sceneryBusy() const { return _scenery.state() != QProcess::NotRunning; }
    int progress() const { return _progress; }
    QString status() const { return _status; }
    QString speed() const { return _speed; }
    QString simLog() const { return _simLog; }

    QString fgRoot() const { return _root + "/fgdata"; }

    /* Aircraft downloaded from the hangar go next to FGData rather than into
       it: FGData is replaced wholesale when the base data is re-fetched, and
       anything put inside it would go with it. */
    QString aircraftDir() const { return _root + "/aircraft"; }

    QVariantList aircraft() const { return _aircraft; }
    QVariantList catalog() const { return _catalog; }
    bool catalogBusy() const
    {
        return _cat.state() != QProcess::NotRunning
            || _acDl.state() != QProcess::NotRunning
            || _acUnzip.state() != QProcess::NotRunning;
    }
    QString hangarStatus() const { return _hangarStatus; }
    /* -1 while there is nothing to show a bar for (fetching the catalogue,
       unpacking); 0..100 during a download. */
    int hangarProgress() const { return _hangarProgress; }

public slots:
    /* An environment variable, for test hooks in QML (FGVIEW_AUTOSTART). */
    QString envValue(const QString& name) const
    {
        return QString::fromLocal8Bit(qgetenv(name.toLocal8Bit().constData()));
    }

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

    /* ---- Aircraft ------------------------------------------------------
       Two lists: what is installed, and what the hangar offers.  The
       chooser used to hold three hardcoded entries, one of which ("j3cub")
       matched neither an installed aircraft nor a catalogue id - the Cub is
       called J3Cub - so picking it started nothing. */

    void refreshAircraft()
    {
        _aircraft.clear();
        /* Both trees: what came with FGData and what was downloaded since. */
        for (const QString& base : { fgRoot() + "/Aircraft", aircraftDir() }) {
            QDirIterator it(base, QStringList() << "*-set.xml",
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                const QString file = QFileInfo(path).fileName();
                const QString id = file.left(file.length() - 8);   // -set.xml

                /* The name shown to the user is the aircraft's own
                   description.  Only the head of the file is read: a -set.xml
                   can be hundreds of kilobytes and the description is in the
                   first few. */
                QString name;
                QString text;
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    text = QString::fromUtf8(f.read(262144));
                    QRegularExpressionMatch m =
                        QRegularExpression("<description>(.*?)</description>",
                                           QRegularExpression::DotMatchesEverythingOption)
                        .match(text.left(16384));
                    if (m.hasMatch()) name = m.captured(1).trimmed();
                }
                if (name.isEmpty()) name = id;

                /* What kind of aircraft, from its tags - for the speed of a
                   start in the air or on final approach.  The A320 family
                   keeps its tags in the file the -set.xml includes, so the
                   includes are read as well. */
                {
                    const QString dir = QFileInfo(path).absolutePath();
                    auto inc = QRegularExpression("include=\"([^\"]+)\"").globalMatch(text.left(4096));
                    int n = 0;
                    while (inc.hasNext() && n++ < 3) {
                        QFile fi(dir + "/" + inc.next().captured(1));
                        if (fi.open(QIODevice::ReadOnly))
                            text += QString::fromUtf8(fi.read(262144));
                    }
                }
                QStringList tags;
                auto tm = QRegularExpression("<tag>\\s*([^<\\s]+)\\s*</tag>").globalMatch(text);
                while (tm.hasNext()) tags << tm.next().captured(1).toLower();
                QString kind;
                if (tags.contains("helicopter")) kind = "helicopter";
                else if (tags.contains("glider")) kind = "glider";
                else if (tags.contains("jet")) kind = "jet";
                else if (tags.contains("turboprop")) kind = "turboprop";
                else if (tags.contains("piston") || tags.contains("propeller")
                         || tags.contains("single-engine")) kind = "piston";

                QVariantMap e;
                e["id"] = id;
                e["name"] = name;
                e["kind"] = kind;
                e["dir"] = QFileInfo(path).absolutePath();
                _aircraft.append(e);
            }
        }
        std::sort(_aircraft.begin(), _aircraft.end(),
                  [](const QVariant& a, const QVariant& b) {
                      return a.toMap()["name"].toString().localeAwareCompare(
                                 b.toMap()["name"].toString()) < 0;
                  });
        emit aircraftChanged();
    }

    /* The catalogue is 1.7 MB of XML listing 648 aircraft.  It is fetched
       with curl rather than QNetworkAccessManager for the same reason the
       base data is: Qt 5.6 here is built against an OpenSSL the system no
       longer has, so its TLS does not work. */
    void fetchCatalog()
    {
        if (catalogBusy()) return;
        _hangarStatus = tr("Fetching the aircraft list…");
        _hangarProgress = -1;
        _cat.setProcessChannelMode(QProcess::MergedChannels);
        _cat.start("curl", QStringList()
                   << "-sSL" << "--max-time" << "180"
                   << "-o" << _root + "/catalog.xml"
                   << "http://mirrors.ibiblio.org/flightgear/ftp/Aircraft-2020/catalog.xml");
        /* After start(), not before: catalogBusy() asks the processes what
           they are doing, and before start() they are doing nothing - the
           view would bind to "idle" and never hear otherwise until the
           download had already finished. */
        emit catalogChanged();
    }

    void installAircraft(const QString& id, const QString& dir, const QString& url)
    {
        if (catalogBusy()) return;
        QDir().mkpath(aircraftDir());
        _pendingId = id;
        _pendingDir = dir;
        _hangarStatus = tr("Downloading %1…").arg(id);
        _hangarProgress = 0;
        QFile::remove(_root + "/aircraft.zip");
        /* aria2c rather than curl, for the progress: it prints a summary
           line once a second that carries the percentage, and the aircraft
           are large - the Cub alone is 66 MB. */
        _acDl.setProcessChannelMode(QProcess::MergedChannels);
        _acDl.start("aria2c", QStringList()
                    << "-x" << "4" << "-s" << "4" << "-c"
                    << "--summary-interval=1"
                    << "--console-log-level=warn"
                    << "--auto-file-renaming=false"
                    << "--allow-overwrite=true"
                    << "--check-certificate=false"
                    << "-d" << _root << "-o" << "aircraft.zip" << url);
        emit catalogChanged();      // after start(), see fetchCatalog()
    }

    void removeAircraft(const QString& dir)
    {
        /* Only under our own directory: FGData's own aircraft are not ours
           to delete, and a path from elsewhere would be a bad thing to hand
           to removeRecursively(). */
        const QString base = QDir(aircraftDir()).absolutePath();
        const QString target = QDir(dir).absolutePath();
        if (!target.startsWith(base + "/")) {
            _hangarStatus = tr("%1 came with the base data and stays").arg(dir);
            emit catalogChanged();
            return;
        }
        QDir(target).removeRecursively();
        refreshAircraft();
        _hangarStatus = tr("Removed");
        emit catalogChanged();
    }

    QVariantList scenarios() const { return _scenarios; }

    /* The scenario files in FGData/AI: name and description of the
       scenario, the carrier in it if there is one (the aircraft can start
       on its deck with --carrier), and where its first object is - most
       demos sit off San Francisco, and it helps to say so. */
    void refreshScenarios()
    {
        _scenarios.clear();
        QDir dir(fgRoot() + "/AI");
        for (const QFileInfo& fi : dir.entryInfoList(QStringList() << "*.xml", QDir::Files, QDir::Name)) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString x = QString::fromUtf8(f.readAll());
            if (!x.contains("<scenario")) continue;
            const QRegularExpression::PatternOption dot = QRegularExpression::DotMatchesEverythingOption;
            QVariantMap e;
            e["id"] = fi.completeBaseName();
            /* the scenario's own name and description come before the first entry */
            const QString head = x.section("<entry", 0, 0);
            auto m = QRegularExpression("<name>(.*?)</name>", dot).match(head);
            e["name"] = m.hasMatch() ? m.captured(1).trimmed() : fi.completeBaseName();
            m = QRegularExpression("<description>(.*?)</description>", dot).match(head);
            e["description"] = m.hasMatch() ? m.captured(1).simplified() : QString();
            QString carrier;
            double lat = 0.0, lon = 0.0;
            QRegularExpression entryRe("<entry>(.*?)</entry>", dot);
            auto it = entryRe.globalMatch(x);
            while (it.hasNext()) {
                const QString entry = it.next().captured(1);
                auto t = QRegularExpression("<type>(.*?)</type>", dot).match(entry);
                auto n = QRegularExpression("<name>(.*?)</name>", dot).match(entry);
                auto la = QRegularExpression("<latitude>(.*?)</latitude>", dot).match(entry);
                auto lo = QRegularExpression("<longitude>(.*?)</longitude>", dot).match(entry);
                auto fp = QRegularExpression("<flightplan>(.*?)</flightplan>", dot).match(entry);
                if (carrier.isEmpty() && t.hasMatch() && t.captured(1).trimmed() == "carrier" && n.hasMatch())
                    carrier = n.captured(1).trimmed();
                if (lat == 0.0 && lon == 0.0 && la.hasMatch() && lo.hasMatch()) {
                    lat = la.captured(1).trimmed().toDouble();
                    lon = lo.captured(1).trimmed().toDouble();
                }
                /* Most objects that are not placed by coordinates follow a
                   flight plan (the KSFO departures, trains, ships): they are
                   where its first waypoint is. */
                if (lat == 0.0 && lon == 0.0 && fp.hasMatch())
                    firstWaypoint(fp.captured(1).trimmed(), &lat, &lon);
            }
            /* the trains name their flight plan once in the parameters, and
               the entries point at it with an alias */
            if (carrier.isEmpty() && lat == 0.0 && lon == 0.0) {
                auto fp = QRegularExpression("<flightplan>([^<]+)</flightplan>").match(x);
                if (fp.hasMatch())
                    firstWaypoint(fp.captured(1).trimmed(), &lat, &lon);
            }
            e["carrier"] = carrier;
            e["lat"] = lat;
            e["lon"] = lon;
            e["airport"] = QString();
            e["airportLabel"] = QString();
            e["airportLat"] = 0.0;
            e["airportLon"] = 0.0;
            _scenarios.append(e);
        }
        nearestAirports();
        emit aircraftChanged();
    }

    /* Where a flight plan in FGData/AI/FlightPlans is: its first waypoint on
       the ground, or the first waypoint if none is - the shuttle's plan
       begins 110 km out and ends on the dry lake at Edwards. */
    void firstWaypoint(const QString& file, double* lat, double* lon) const
    {
        QFile f(fgRoot() + "/AI/FlightPlans/" + file);
        if (!f.open(QIODevice::ReadOnly)) return;
        const QString x = QString::fromUtf8(f.readAll());
        const QRegularExpression::PatternOption dot = QRegularExpression::DotMatchesEverythingOption;
        bool haveFirst = false;
        auto it = QRegularExpression("<wpt>(.*?)</wpt>", dot).globalMatch(x);
        while (it.hasNext()) {
            const QString w = it.next().captured(1);
            auto la = QRegularExpression("<lat>(.*?)</lat>", dot).match(w);
            auto lo = QRegularExpression("<lon>(.*?)</lon>", dot).match(w);
            if (!la.hasMatch() || !lo.hasMatch()) continue;
            const bool ground = QRegularExpression("<on-ground>\\s*(true|1)\\s*</on-ground>").match(w).hasMatch();
            if (ground || !haveFirst) {
                *lat = la.captured(1).trimmed().toDouble();
                *lon = lo.captured(1).trimmed().toDouble();
                haveFirst = true;
            }
            if (ground) return;
        }
    }

    /* A scenario that is somewhere - off San Francisco, on a railway in
       Germany - does nothing for an aircraft at the airport picked on the
       start page: FlightGear loads the objects where they are, and nobody
       sees them.  So such a scenario brings its own departure airport from
       the list the airport picker uses: the airport it is on, if one is
       within 3 km (the glider tow at Reid-Hillview), otherwise a large one
       within 40 km (the ships in the bay start at KSFO rather than at a
       heliport a little closer), otherwise the nearest. */
    void nearestAirports()
    {
        struct Best { double d = 1e18; QVariantList a; };
        QVector<Best> large(_scenarios.size()), any(_scenarios.size());
        bool wanted = false;
        for (const QVariant& v : _scenarios) {
            const QVariantMap m = v.toMap();
            if (m["carrier"].toString().isEmpty() && (m["lat"].toDouble() != 0.0 || m["lon"].toDouble() != 0.0))
                wanted = true;
        }
        if (!wanted) return;
        const auto dist = [](double la1, double lo1, double la2, double lo2) {
            const double r = M_PI / 180.0;
            const double a = std::sin((la2 - la1) * r / 2), b = std::sin((lo2 - lo1) * r / 2);
            return 2 * 6371.0 * std::asin(std::sqrt(a * a + std::cos(la1 * r) * std::cos(la2 * r) * b * b));
        };
        QDir dir(AIRPORT_DATA);
        for (const QFileInfo& fi : dir.entryInfoList(QStringList() << "*.json", QDir::Files)) {
            if (fi.fileName() == "countries.json") continue;
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            for (const QString& size : {QStringLiteral("large"), QStringLiteral("small")}) {
                for (const QJsonValue& av : o[size].toArray()) {
                    const QJsonArray a = av.toArray();
                    if (a.size() < 5) continue;
                    const double la = a[3].toDouble(), lo = a[4].toDouble();
                    for (int i = 0; i < _scenarios.size(); ++i) {
                        const QVariantMap m = _scenarios[i].toMap();
                        if (!m["carrier"].toString().isEmpty()) continue;
                        const double sla = m["lat"].toDouble(), slo = m["lon"].toDouble();
                        if (sla == 0.0 && slo == 0.0) continue;
                        const double d = dist(sla, slo, la, lo);
                        if (d < any[i].d) { any[i].d = d; any[i].a = a.toVariantList(); }
                        if (size == "large" && d < large[i].d) { large[i].d = d; large[i].a = a.toVariantList(); }
                    }
                }
            }
        }
        for (int i = 0; i < _scenarios.size(); ++i) {
            const QVariantList& a = any[i].d <= 3.0 ? any[i].a
                                  : large[i].d <= 40.0 ? large[i].a : any[i].a;
            if (a.isEmpty() || any[i].d > 200.0) continue;
            QVariantMap m = _scenarios[i].toMap();
            m["airport"] = a[0].toString();
            m["airportLabel"] = a[1].toString() + " (" + a[0].toString() + ")";
            m["airportLat"] = a[3].toDouble();
            m["airportLon"] = a[4].toDouble();
            _scenarios[i] = m;
        }
    }

    /* Scenery for somewhere else than the departure airport - a lesson
       repositions the aircraft (the c172p's are at Hilo, Hawaii).  Same
       check-then-fetch as before a start; sceneryReady() when done, fetched
       or not, so the caller can go on. */
    void fetchSceneryFor(double lat, double lon)
    {
        if (_scenery.state() != QProcess::NotRunning) return;
        _sceneryThen = SceneryThenSignal;
        beginScenery(lat, lon, false);
    }

    void startSim(const QString& aircraft = "c172p",
                  const QString& airport  = "LOWW",
                  const QString& backend  = "gles3",
                  bool startInAir = false,
                  bool sound = false,
                  const QStringList& extraProps = QStringList(),
                  double airportLat = 0.0,
                  double airportLon = 0.0,
                  bool refreshScenery = false)
    {
        if (simRunning() || !dataReady()) return;

        _backend = backend;

        /* The control protocol lives in FGData, which is downloaded once and
           then kept, so it is rewritten on every start.  It used to be
           copied from /opt/fgfs/share, which tied the field order to the
           runtime package; the sender is in this application, so the
           definition is too.

           One throttle field per engine, eight of them: a six-engined
           An-225 with the lever wired to engine[0] alone taxied on one
           engine, and the generic protocol cannot loop.  Fields for
           engines an aircraft does not have only create unused
           properties. */
        {
            static const char* protocol =
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<!-- fgtouch: the control channel from harbour-fgview to fgfs, UDP port\n"
                "     5501, one comma separated line per update.  Written by the app;\n"
                "     the field order matches ControlSender::sendPacket. -->\n"
                "<PropertyList><generic><input>\n"
                "<line_separator>newline</line_separator><var_separator>,</var_separator>\n"
                "<chunk><name>aileron</name><node>/controls/flight/aileron</node><type>float</type></chunk>\n"
                "<chunk><name>elevator</name><node>/controls/flight/elevator</node><type>float</type></chunk>\n"
                "<chunk><name>rudder</name><node>/controls/flight/rudder</node><type>float</type></chunk>\n"
                "<chunk><name>throttle0</name><node>/controls/engines/engine[0]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle1</name><node>/controls/engines/engine[1]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle2</name><node>/controls/engines/engine[2]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle3</name><node>/controls/engines/engine[3]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle4</name><node>/controls/engines/engine[4]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle5</name><node>/controls/engines/engine[5]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle6</name><node>/controls/engines/engine[6]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>throttle7</name><node>/controls/engines/engine[7]/throttle</node><type>float</type></chunk>\n"
                "<chunk><name>brake</name><node>/controls/gear/brake-parking</node><type>float</type></chunk>\n"
                "<chunk><name>flaps</name><node>/controls/flight/flaps</node><type>float</type></chunk>\n"
                "<chunk><name>gear</name><node>/controls/gear/gear-down</node><type>bool</type></chunk>\n"
                "<!-- current-engine is what the cockpit lever's animation reads -->\n"
                "<chunk><name>throttle-lever</name><node>/controls/engines/current-engine/throttle</node><type>float</type></chunk>\n"
                "</input></generic></PropertyList>\n";
            QDir().mkpath(fgRoot() + "/Protocol");
            QFile f(fgRoot() + "/Protocol/fgtouch.xml");
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(protocol);
        }

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

        /* Sound.  OpenAL Soft on this device is built with exactly one
           backend, pulse, and a PulseAudio client finds its socket under
           XDG_RUNTIME_DIR.  We have to point that at /run/display for
           Wayland, and there is no pulse directory there - so the simulator
           could never have reached the sound server, whatever else was set.
           Take the socket from our own environment before overwriting it,
           rather than writing a user id into the source. */
        const QString userRuntime = env.value("XDG_RUNTIME_DIR");
        if (!userRuntime.isEmpty())
            env.insert("PULSE_SERVER", "unix:" + userRuntime + "/pulse/native");

        env.insert("XDG_RUNTIME_DIR", "/run/display");
        env.insert("WAYLAND_DISPLAY", "wayland-0");
        env.insert("FGFS_SHM", "1");
        env.remove("GALLIUM_DRIVER");

        QString binary;
        if (backend == "gles2" || backend == "gles3") {
            /* Nativ auf hybris-EGL: kein Mesa, kein Zink. Dafuer
               ohne GUI, HUD, Canvas und Anflugbefeuerung. */
            /* The GLES trees moved out of /opt in
               fgfs-sailfish-gles-2020.3.19-9: they are 793 MB and the root
               filesystem has under 2 GB free, while /home has 180 GB.  The
               old location is still accepted, so a system that has the new
               application but not yet the new runtime still starts. */
            const QString name = (backend == "gles3") ? "osg-gles3" : "osg-gles";
            const QString bin  = (backend == "gles3") ? "fgfs-gles3" : "fgfs-gles";
            auto tree = [](const QString& leaf) {
                for (const QString& base : { QStringLiteral("/home/.system/fgfs"),
                                             QStringLiteral("/opt") })
                    if (QFileInfo::exists(base + "/" + leaf)) return base + "/" + leaf;
                return QStringLiteral("/home/.system/fgfs/") + leaf;
            };
            const QString root = tree(name);
            env.insert("LD_LIBRARY_PATH", root + "/lib");
            env.insert("OSG_LIBRARY_PATH", root + "/lib/osgPlugins-3.6.5");
            /* Kein Zero-Copy: hybris-EGL kennt kein
               EGL_EXT_image_dma_buf_import, der Simulator koennte den
               dmabuf gar nicht als Renderziel benutzen und die App
               saehe einen leeren Puffer. Also Readback. */
            env.insert("FGFS_DMA_HEAP", "/dev/null");
            /* Vertex array objects.  Draw is bound by the number of draw
               calls, not by fill rate - rendering a quarter of the pixels
               leaves draw time unchanged - so carrying the per-drawable
               attribute setup in a VAO instead of repeating it on every
               call pays off.  Measured on LOWW with the c172p: frame
               74.1 -> 66.6 ms, draw 39.3 -> 31.3.  VERTEX_BUFFER_OBJECT is
               the weaker variant (draw 43.0). */
            env.insert("OSG_VERTEX_BUFFER_HINT", "VERTEX_ARRAY_OBJECT");
            binary = tree(bin) + "/bin/fgfs";
        } else {
            env.insert("LD_LIBRARY_PATH", "/opt/mesa-zink/lib64:/opt/fgfs/lib");
            env.insert("__EGL_VENDOR_LIBRARY_DIRS",
                       "/opt/mesa-zink/share/glvnd/egl_vendor.d");
            env.insert("EGL_PLATFORM", "wayland");
            env.insert("MESA_LOADER_DRIVER_OVERRIDE", "zink");
            /* Descriptor-Verwaltung: bringt bei Modellen mit vielen
               Zustandswechseln rund 30 Prozent. */
            env.insert("ZINK_DESCRIPTORS", "lazy");
            binary = QStringLiteral("/opt/fgfs/bin/fgfs");
        }

        /* The simulator is started once the scenery is known to be there;
           beginScenery() takes it from here.  It cannot be an argument to
           the binary: this starts FlightGear itself, not the fgfs-run
           wrapper, and FlightGear rejects options it does not know. */
        _simBinary = binary;
        _simEnv = env;
        _simArgs = QStringList()
                   << "--fg-root=" + fgRoot()
                   /* Downloaded aircraft live outside FGData, so FlightGear
                      has to be told where to look. */
                   << "--fg-aircraft=" + aircraftDir()
                   /* --enable/--disable-terrasync comes from the settings
                      page in extraProps (in-flight scenery updates) */
                   /* --enable/--disable-ai-models comes from the start page
                      in extraProps: on for a scenario, off otherwise */
                   /* real weather on or off comes from the settings page,
                      as --enable/--disable-real-weather-fetch in extraProps */
                   /* Sound off unless asked for: it costs frame time, and
                      the default configuration is the fast one. */
                   << (sound ? QStringList{} : QStringList{ "--disable-sound" })
                   << "--prop:/sim/rendering/shadows/enabled=false"
                   /* PUI-Menueleiste aus: PLIBs glBitmap-Schriften
                      stuerzen unter Zink in tc_texture_map ab, und
                      auf dem Telefon ist die Leiste ohnehin
                      unbedienbar. */
                   << "--prop:/sim/menubar/visibility=false"
                   << "--prop:/sim/rendering/multi-sample-buffers=0"
                   << "--generic=socket,in,30,,5501,udp,fgtouch"
                   << "--telnet=5401"
                   /* The engine start runs a Nasal snippet over that
                      channel (one script for any number and kind of
                      engines); FlightGear refuses Nasal from sockets
                      without this. */
                   << "--allow-nasal-from-sockets"
                   << "--aircraft=" + aircraft
                   << "--airport=" + airport
                   /* No --timeofday here any more: the settings page sends
                      one, and the last occurrence wins, so a hardcoded noon
                      would only be confusing to read. */
                   /* --disable-ai-models leaves the traffic manager on; it
                      filled the scene with fifty scheduled aircraft, each
                      with its own motion, model and draw calls. */
                   /* traffic, model rate, trees and the rest come from the
                      settings page (extraProps), with the measured values as
                      defaults; --disable-ai-models stays, the scenario
                      objects are not wanted either way */
                   /* Draw on its own thread; the update phase then overlaps
                      with the draw - measured 84 -> 47 ms per frame. */
                   << (backend != "zink"
                       ? "--prop:/sim/rendering/multithreading-mode=DrawThreadPerContext"
                       : "--prop:/sim/rendering/multithreading-mode=SingleThreaded")
                   /* In the air the engine comes up by itself.  On the ground
                      the c172p wants the whole start-up procedure, and its
                      Nasal scripts reset magnetos and battery behind us. */
                   << (startInAir ? QStringList{ "--altitude=3000", "--vc=90" }
                                  : QStringList{})
                   << extraProps;

        beginScenery(airportLat, airportLon, refreshScenery);
    }

    /* ---------------------------------------------------------------- scenery
       Before the simulator starts, make sure the TerraSync tiles around the
       departure airport are there.  fgfs-scenery answers --check offline
       from the regions it has finished, so a start with the scenery in
       place costs no network round trip and works with the radio off; only
       when the answer is "missing" does it go to the mirrors.

       A fresh device showed nothing but water: the scenery on the
       development phone had been fetched by hand, and the application
       never fetched any. */
    /* what to do once the scenery is settled */
    void sceneryDone()
    {
        if (_sceneryThen == SceneryThenSignal) {
            _sceneryThen = SceneryThenLaunch;
            _sceneryPhase = SceneryIdle;
            emit stateChanged();
            emit sceneryReady();
        } else {
            launchSim();
        }
    }

    void beginScenery(double lat, double lon, bool refresh)
    {
        /* No coordinates (an airport picked with a version whose lists had
           none): start as before rather than fetch the wrong region. */
        if (lat == 0.0 && lon == 0.0) { sceneryDone(); return; }
        if (!QFileInfo::exists(SCENERY_TOOL)) { sceneryDone(); return; }

        _sceneryLat = lat;
        _sceneryLon = lon;
        if (refresh) { startSceneryFetch(); return; }

        _sceneryPhase = SceneryCheck;
        _status = tr("Checking the scenery around the airport");
        emit progressChanged();
        emit stateChanged();
        _scenery.start(SCENERY_TOOL, sceneryArgs() << "--check");
    }

    void startSceneryFetch()
    {
        _sceneryPhase = SceneryFetch;
        _progress = 0;
        _speed.clear();
        _status = tr("Fetching the scenery around the airport — a few hundred MB on the first start");
        emit progressChanged();
        emit stateChanged();
        _scenery.start(SCENERY_TOOL, sceneryArgs());
    }

    QStringList sceneryArgs() const
    {
        return QStringList()
               << "--lat" << QString::number(_sceneryLat, 'f', 4)
               << "--lon" << QString::number(_sceneryLon, 'f', 4)
               << "--radius" << "1"
               << "--target" << QDir::homePath() + "/.fgfs/TerraSync"
               /* Only these two are organised by tile, so only these two
                  can be fetched for a region.  Airports and Models are
                  world-wide trees of thousands of index files; walking
                  them held the start for twenty minutes. */
               << "--subtrees" << "Terrain,Objects";
    }

    void launchSim()
    {
        _sceneryPhase = SceneryIdle;
        _sim.setProcessEnvironment(_simEnv);
        _sim.setProcessChannelMode(QProcess::MergedChannels);
        _simLog.clear();
        emit simLogChanged();
        _sim.start(_simBinary, _simArgs);

        _status = tr("Simulator starting — this takes a minute or two");
        emit stateChanged();
        emit progressChanged();
    }

    void stopSim()
    {
        /* The scenery fetch first, and before the flag that would start the
           simulator when it ends.  fgfs-scenery passes the signal on to its
           aria2c, so the download really stops instead of running on
           unattached. */
        if (_scenery.state() != QProcess::NotRunning) {
            _sceneryPhase = SceneryIdle;
            _sceneryThen = SceneryThenLaunch;
            _scenery.terminate();
            if (!_scenery.waitForFinished(3000)) _scenery.kill();
            _status = tr("Stopped");
            _progress = 0;
            _speed.clear();
            emit progressChanged();
        }
        _sceneryPhase = SceneryIdle;
        if (_sim.state() != QProcess::NotRunning) {
            _sim.terminate();
            if (!_sim.waitForFinished(3000)) _sim.kill();
        }
        /* Segment und Socket aufraeumen - sonst verbindet sich der
           naechste Start mit einem toten Socket. */
        ::unlink("/dev/shm/fgfs-frame");
        ::unlink("/tmp/fgfs-frame.sock");
        emit stateChanged();
    }

private slots:
    /* The catalogue has arrived: turn 1.7 MB of XML into the handful of
       fields the list needs.  Read with regular expressions rather than a
       parser because the file is machine-generated and utterly regular. */
    void onCatalogFinished(int, QProcess::ExitStatus)
    {
        _catalog.clear();
        QFile f(_root + "/catalog.xml");
        if (!f.open(QIODevice::ReadOnly)) {
            _hangarStatus = tr("Could not fetch the aircraft list");
            emit catalogChanged();
            return;
        }
        const QString doc = QString::fromUtf8(f.readAll());

        QRegularExpression pkgRe("<package>(.*?)</package>",
                                 QRegularExpression::DotMatchesEverythingOption);
        QRegularExpression idRe("<id>(.*?)</id>");
        QRegularExpression nameRe("<name>(.*?)</name>");
        QRegularExpression dirRe("<dir>(.*?)</dir>");
        /* Not simply the first <url>: a package lists its variants first,
           and each carries <preview><url>...jpg</url></preview> blocks.  The
           download addresses come after <dir>, so the first URL in the block
           is usually a screenshot - taking it fetched a JPEG and unzip then
           refused it.  The zip is what is wanted, so ask for the zip. */
        QRegularExpression urlRe("<url>([^<]*\\.zip)</url>");
        QRegularExpression fdmRe("<FDM[^>]*>(\\d+)</FDM>");
        QRegularExpression modelRe("<model[^>]*>(\\d+)</model>");

        QRegularExpressionMatchIterator it = pkgRe.globalMatch(doc);
        while (it.hasNext()) {
            const QString p = it.next().captured(1);
            QRegularExpressionMatch mi = idRe.match(p);
            QRegularExpressionMatch mn = nameRe.match(p);
            QRegularExpressionMatch mu = urlRe.match(p);
            if (!mi.hasMatch() || !mn.hasMatch() || !mu.hasMatch()) continue;
            QRegularExpressionMatch md = dirRe.match(p);
            QRegularExpressionMatch mf = fdmRe.match(p);
            QRegularExpressionMatch mm = modelRe.match(p);

            QVariantMap e;
            e["id"] = mi.captured(1);
            e["name"] = mn.captured(1).trimmed();
            e["dir"] = md.hasMatch() ? md.captured(1) : mi.captured(1);
            e["url"] = mu.captured(1);
            /* The catalogue rates flight model and 3D model from 0 to 5.
               Shown so that a finished airliner is not buried among
               abandoned sketches. */
            e["fdm"] = mf.hasMatch() ? mf.captured(1).toInt() : 0;
            e["model"] = mm.hasMatch() ? mm.captured(1).toInt() : 0;
            _catalog.append(e);
        }

        _hangarStatus = _catalog.isEmpty()
                        ? tr("The aircraft list came back empty")
                        : tr("%1 aircraft available").arg(_catalog.size());
        emit catalogChanged();
    }

    /* aria2 prints e.g.
           [#1a2b3c 12MiB/65MiB(18%) CN:4 DL:2.1MiB ETA:25s]
       once a second.  The percentage is all the bar needs; the speed is
       worth showing because a 66 MB aircraft on a slow line is a long wait
       and a still bar looks like a hang. */
    void onAircraftOutput()
    {
        const QString chunk = QString::fromUtf8(_acDl.readAllStandardOutput());

        /* The last match in the chunk, not the first: a chunk can hold
           several of aria2's once-a-second lines, and the first of those is
           the oldest - the bar would trail behind by however many lines
           arrived together. */
        QRegularExpressionMatch m, d;
        QRegularExpressionMatchIterator mi =
            QRegularExpression("\\((\\d+)%\\)").globalMatch(chunk);
        while (mi.hasNext()) m = mi.next();
        QRegularExpressionMatchIterator di =
            QRegularExpression("DL:([0-9.]+[KMG]?i?B)").globalMatch(chunk);
        while (di.hasNext()) d = di.next();

        if (m.hasMatch()) _hangarProgress = m.captured(1).toInt();

        /* The megabyte counter as well as the percentage: a fast download
           produces only a handful of summary lines, and a bar that jumps
           from 0 to gone looks like a failure. The byte figure at least
           shows that something moved. */
        QRegularExpressionMatch sz;
        QRegularExpressionMatchIterator si =
            QRegularExpression("([0-9.]+[KMG]?i?B/[0-9.]+[KMG]?i?B)").globalMatch(chunk);
        while (si.hasNext()) sz = si.next();
        QString detail = QString::number(_hangarProgress) + " %";
        if (sz.hasMatch()) detail += ", " + sz.captured(1);
        if (d.hasMatch())  detail += ", " + d.captured(1) + "/s";
        _hangarStatus = tr("Downloading %1 — %2").arg(_pendingId).arg(detail);
        emit catalogChanged();
    }

    void onAircraftDownloaded(int code, QProcess::ExitStatus)
    {
        const QString zip = _root + "/aircraft.zip";
        if (code != 0 || QFileInfo(zip).size() < 1024) {
            _hangarStatus = tr("Download of %1 failed").arg(_pendingId);
            QFile::remove(zip);
            emit catalogChanged();
            return;
        }
        _hangarStatus = tr("Unpacking %1…").arg(_pendingId);
        _hangarProgress = -1;
        emit catalogChanged();
        _acUnzip.setProcessChannelMode(QProcess::MergedChannels);
        _acUnzip.start("unzip", QStringList()
                       << "-o" << "-q" << zip << "-d" << aircraftDir());
    }

    void onAircraftUnpacked(int code, QProcess::ExitStatus)
    {
        QFile::remove(_root + "/aircraft.zip");
        refreshAircraft();
        _hangarProgress = -1;
        _hangarStatus = (code == 0) ? tr("%1 installed").arg(_pendingId)
                                    : tr("Unpacking %1 failed").arg(_pendingId);
        emit catalogChanged();
    }

    void onSimOutput()
    {
        const QString chunk = QString::fromUtf8(_sim.readAllStandardOutput());
        if (chunk.isEmpty()) return;

        /* alles mitschreiben, damit man nach einem Absturz nachsehen kann */
        if (!_logFile.isOpen()) {
            _logFile.setFileName(_root + "/fgfs.log");
            _logFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
        }
        if (_logFile.isOpen()) {
            _logFile.write(chunk.toUtf8());
            _logFile.flush();
        }

        /* die letzten Zeilen fuer die Anzeige vorhalten */
        _simLog += chunk;
        const int maxChars = 4000;
        if (_simLog.size() > maxChars)
            _simLog = _simLog.right(maxChars);
        emit simLogChanged();

        /* aussagekraeftige Zeilen in die Statuszeile heben */
        const QStringList lines = chunk.split('\n', QString::SkipEmptyParts);
        for (const QString& raw : lines) {
            const QString l = raw.trimmed();
            if (l.contains("FATAL") || l.contains("unable to create")) {
                _status = l;
                emit progressChanged();
            } else if (l.contains("Loading tile")
                       || l.contains("Scenery loaded")
                       || l.contains("initializing JSBsim")
                       || l.contains("Trim complete")
                       || l.contains("Splash screen")
                       || l.contains("Welcome aboard")) {
                _status = l.section(']', 1).trimmed();
                if (_status.isEmpty()) _status = l;
                emit progressChanged();
            }
        }
    }

    /* fgfs-scenery reports on stderr, aria2c's progress lines among them. */
    void onSceneryOutput()
    {
        const QString out = QString::fromUtf8(_scenery.readAllStandardOutput())
                          + QString::fromUtf8(_scenery.readAllStandardError());
        _simLog += out;
        if (_simLog.size() > 4000) _simLog = _simLog.right(4000);
        emit simLogChanged();

        /* [#4926fa 1.0GiB/1.6GiB(60%) CN:8 DL:2.4MiB ETA:4m35s] */
        static const QRegularExpression rePct("\\((\\d+)%\\)");
        static const QRegularExpression reDl("DL:([0-9.]+[KMG]i?B)");
        static const QRegularExpression reEta("ETA:([0-9dhms]+)");
        const auto m = rePct.match(out);
        const auto d = reDl.match(out);
        const auto e = reEta.match(out);
        if (m.hasMatch()) _progress = m.captured(1).toInt();
        if (d.hasMatch()) {
            _speed = d.captured(1) + "/s";
            if (e.hasMatch()) _speed += "  ETA " + e.captured(1);
        }
        /* Before aria2c starts there is a long walk over the indexes with
           no percentage of its own; say what it is doing. */
        if (out.contains("Durchsuche ") || out.contains("Indizes gelesen")) {
            const QString line = out.section("Durchsuche ", -1).section('\n', 0, 0);
            if (!line.isEmpty() && out.contains("Durchsuche "))
                _status = tr("Looking through the scenery index: %1").arg(line.trimmed());
        }
        if (m.hasMatch() || d.hasMatch() || out.contains("Durchsuche "))
            emit progressChanged();
    }

    void onSceneryFinished(int code, QProcess::ExitStatus status)
    {
        const bool crashed = status == QProcess::CrashExit;
        if (_sceneryPhase == SceneryCheck) {
            /* 0 = a finished fetch covers the airport.  Anything else,
               including a crash or a fgfs-scenery too old to know --check,
               means: fetch. */
            if (code == 0 && !crashed) {
                _status = tr("Scenery for the airport is present");
                emit progressChanged();
                sceneryDone();
            } else if (code == 2 && !crashed) {
                /* Tiles on disk from before the record existed: fly now
                   rather than hold the start for a comparison with the
                   servers; the settings switch does that on demand. */
                _status = tr("Scenery for the airport is on the device");
                emit progressChanged();
                sceneryDone();
            } else {
                startSceneryFetch();
            }
            return;
        }
        if (_sceneryPhase == SceneryFetch) {
            _progress = 0;
            _speed.clear();
            _status = (code == 0 && !crashed)
                    ? tr("Scenery fetched")
                    : tr("The scenery could not be fetched (no network?) — starting with what is there");
            emit progressChanged();
            sceneryDone();
        }
    }

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
    void aircraftChanged();
    void catalogChanged();

    void stateChanged();
    void progressChanged();
    void sceneryReady();
    void simLogChanged();

private:
    QString _root;
    static const qint64  ARCHIVE_BYTES   = 1789370768LL;
    static const quint64 EXTRACTED_BYTES = 2705459200ULL;

    static constexpr const char* SCENERY_TOOL = "/opt/fgfs/bin/fgfs-scenery";
    /* the airport list the picker pages read, one JSON file per country */
    static constexpr const char* AIRPORT_DATA = "/usr/share/harbour-fgview/airports";
    enum SceneryPhase { SceneryIdle, SceneryCheck, SceneryFetch };
    enum SceneryThen { SceneryThenLaunch, SceneryThenSignal };
    SceneryThen _sceneryThen = SceneryThenLaunch;
    QVariantList _scenarios;

    QProcess _dl, _tar, _sim, _resolve, _scenery;
    SceneryPhase _sceneryPhase = SceneryIdle;
    double _sceneryLat = 0.0, _sceneryLon = 0.0;
    QString _simBinary;
    QStringList _simArgs;
    QProcessEnvironment _simEnv;
    QProcess _cat, _acDl, _acUnzip;
    QVariantList _aircraft, _catalog;
    QString _hangarStatus;
    int _hangarProgress = -1;
    QString _pendingId, _pendingDir;
    QString  _backend;
    QTimer _heartbeat;
    QTimer _extractTick;
    QString _simLog;
    QFile _logFile;
    QNetworkAccessManager _nam;
    bool _resolving = false;
    int _progress = 0;
    QString _status;
    QString _speed;
};

#endif // FGRUNTIME_H
