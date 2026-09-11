import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.fgview 1.0
import Nemo.Configuration 1.0

Page {
    id: page

    // So the jump into the cockpit happens once per simulator run and not
    // again every time some other state changes.
    property bool cockpitOpened: false
    allowedOrientations: Orientation.All

    property FgRuntime rt

    // the same group the settings page writes
    ConfigurationGroup {
        id: simCfg
        path: "/apps/harbour-fgview/sim"
        property real vegetation: 0.0
        property int  modelHz: 60
        property int  trafficLevel: 0
        property bool buildings: false
        property int  visibility: 0
        property int  clouds: 2
        property int  timeOfDay: 2
        property bool autoCoord: false
        property int  frameLimit: 0
        property int  detailRange: 1500
        property int  filtering: 1
        property bool particles: true
        property bool sound: false
        property string aircraft: "c172p"
        property string aircraftLabel: "Cessna 172P"
        property string airport: "LOWW"
        property string airportLabel: "Wien Schwechat (LOWW)"
        // Where the departure airport is, for the scenery fetch - and which
        // airport the coordinates belong to.  An airport picked with 0.9.6
        // or earlier is stored without any, and the group would hand out
        // these defaults for it: the app would then fetch Vienna for
        // Frankfurt and, worse, record it as done.
        property real airportLat: 48.110
        property real airportLon: 16.570
        property string airportCoordsIcao: "LOWW"
        property bool sceneryRefresh: false
        property bool sceneryInFlight: false
        property bool realWeather: false
        property bool pauseInBackground: true
        // AI scenario: file name in FGData/AI without .xml, "" for none;
        // the carrier in it, if any, is where the aircraft then starts
        property string scenario: ""
        property string scenarioLabel: ""
        property string scenarioCarrier: ""
        property real scenarioLat: 0
        property real scenarioLon: 0
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: "FlightGear" }

            // ---- base data missing --------------------------------

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: rt !== null && !rt.dataReady

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.highlightColor
                    text: qsTr("The base data (FGData) is missing. It contains aircraft, instruments and scenery definitions and is about 1.7 GB.")
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: qsTr("The download uses eight parallel connections and can be interrupted and resumed at any time. Wi-Fi recommended.")
                }

                Item { width: 1; height: Theme.paddingMedium }

                ProgressBar {
                    width: parent.width
                    visible: rt !== null && rt.busy && rt.progress > 0
                    minimumValue: 0
                    maximumValue: 100
                    value: rt ? rt.progress : 0
                    label: rt ? rt.status : ""
                    valueText: rt ? (rt.progress + " %  " + rt.speed) : ""
                }

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Medium
                    running: rt !== null && rt.busy && rt.progress === 0
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    visible: rt !== null && rt.busy
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: rt ? rt.status : ""
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    visible: rt !== null && !rt.busy && rt.status.length > 0
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: rt ? rt.status : ""
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (rt && rt.busy) ? qsTr("Cancel") : qsTr("Download base data")
                    onClicked: {
                        if (!rt) return
                        if (rt.busy) rt.cancelDownload()
                        else rt.downloadData()
                    }
                }
            }

            // ---- ready to fly -------------------------------------

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: rt !== null && rt.dataReady

                Label {
                    x: Theme.horizontalPageMargin
                    color: Theme.highlightColor
                    text: qsTr("Base data installed")
                }

                // Aircraft: whatever is installed, plus a way to the
                // hangar.  The three fixed entries this replaces included
                // "j3cub", which is neither in FGData nor in the catalogue -
                // the Cub is called J3Cub - so choosing it started nothing.
                ValueButton {
                    id: aircraftButton
                    label: qsTr("Aircraft")
                    value: simCfg.aircraftLabel !== "" ? simCfg.aircraftLabel
                                                       : simCfg.aircraft
                    onClicked: {
                        var p = pageStack.push(Qt.resolvedUrl("AircraftPage.qml"),
                                               { rt: rt })
                        p.picked.connect(function(id, label) {
                            simCfg.aircraft = id
                            simCfg.aircraftLabel = label
                        })
                    }
                }

                ComboBox {
                    id: backendBox
                    label: qsTr("Graphics backend")
                    currentIndex: 2
                    menu: ContextMenu {
                        MenuItem { text: qsTr("Zink (complete)") }
                        MenuItem { text: qsTr("GLES2 (native)") }
                        MenuItem { text: qsTr("GLES3 (native)") }
                    }
                    property var ids: ["zink", "gles2", "gles3"]
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    visible: backendBox.currentIndex > 0
                    text: qsTr("The native backends run without Mesa, but "
                             + "have no menus, HUD, glass cockpit displays "
                             + "or approach lights.")
                }

                TextSwitch {
                    id: airborneSwitch
                    text: qsTr("Start in the air")
                    description: qsTr("Starts at 3000 ft with the engine "
                                    + "running. On the ground the engine has "
                                    + "to be started by hand.")
                    checked: false
                }

                // Departure airport: country, then large or small, then
                // the list.  A ComboBox held five hand-picked airports;
                // there are 27000, so the choice is a page of its own now.
                // The last pick is remembered, so the usual case is one tap
                // on Start.
                ValueButton {
                    id: airportButton
                    label: qsTr("Departure airport")
                    value: simCfg.airportLabel !== "" ? simCfg.airportLabel
                                                      : simCfg.airport
                    onClicked: {
                        var p = pageStack.push(Qt.resolvedUrl("AirportCountryPage.qml"))
                        p.picked.connect(function(icao, label, lat, lon) {
                            simCfg.airport = icao
                            simCfg.airportLabel = label
                            simCfg.airportLat = lat
                            simCfg.airportLon = lon
                            simCfg.airportCoordsIcao = icao
                        })
                    }
                }

                // Scenario: FlightGear's AI scenarios - a carrier to land on,
                // tankers, a wingman.  With a carrier the aircraft starts on
                // its deck and the airport above is ignored.
                ValueButton {
                    label: qsTr("Scenario")
                    value: simCfg.scenarioLabel !== "" ? simCfg.scenarioLabel : qsTr("None")
                    onClicked: {
                        var p = pageStack.push(Qt.resolvedUrl("ScenarioPage.qml"), { rt: rt })
                        p.picked.connect(function(id, label, carrier, lat, lon) {
                            simCfg.scenario = id
                            simCfg.scenarioLabel = label
                            simCfg.scenarioCarrier = carrier
                            simCfg.scenarioLat = lat
                            simCfg.scenarioLon = lon
                        })
                    }
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: (rt && rt.simRunning) ? rt.status
                          : qsTr("Before the start the scenery around the airport is checked and, if missing, fetched (a few hundred MB); loading then takes a minute or two.")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (rt && rt.simRunning) ? qsTr("Stop simulator") : qsTr("Start")
                    onClicked: {
                        if (!rt) return
                        if (rt.simRunning) {
                            rt.stopSim()
                        } else {
                            rt.startSim(simCfg.aircraft,
                                        simCfg.airport,
                                        backendBox.ids[backendBox.currentIndex],
                                        airborneSwitch.checked,
                                        simCfg.sound,
                                        [ // At zero, switch the vegetation off outright rather
                                          // than only setting its density to nothing: with
                                          // random-vegetation still true the tile loader keeps
                                          // running the placement work, and the loading stalls
                                          // that produces are far worse than the trees ever cost
                                          // to draw.
                                          "--prop:/sim/rendering/random-vegetation="
                                            + (simCfg.vegetation > 0 ? "true" : "false"),
                                          "--prop:/sim/rendering/vegetation-density=" + simCfg.vegetation,
                                          "--prop:/sim/model-hz=" + simCfg.modelHz,
                                          // Level 0 really has to switch the manager off:
                                          // a proportion of 0.0 still lets one schedule in
                                          // eight through, because the test is "randval >
                                          // proportion" and randval can be 0.
                                          "--prop:/sim/traffic-manager/enabled="
                                            + (simCfg.trafficLevel > 0 ? "true" : "false"),
                                          // 0.10 / 0.35 / 0.50 / 0.80 / 1.0 land on 25, 37.5,
                                          // 50, 75 and 100 percent.  FlightGear compares
                                          // against "rand() & 100", a bitwise and, so only
                                          // 0, 4, 32, 36, 64, 68, 96 and 100 ever come out of
                                          // it - anything between those steps changes nothing.
                                          "--prop:/sim/traffic-manager/proportion="
                                            + [0.0, 0.0, 0.10, 0.50, 0.80, 1.0][simCfg.trafficLevel],
                                          "--prop:/sim/rendering/static-lod/detailed=" + simCfg.detailRange,
                                          "--prop:/sim/rendering/filtering=" + simCfg.filtering,
                                          "--prop:/sim/rendering/particles=" + (simCfg.particles ? "true" : "false"),
                                          "--prop:/sim/rendering/random-buildings="
                                            + (simCfg.buildings ? "true" : "false"),
                                          // Two separate things: the flat layers live in
                                          // /environment/clouds/status, the volumetric ones in
                                          // clouds3d-enable.  "Flat layers" therefore means
                                          // status on and 3d off, not one setting turned down.
                                          "--prop:/environment/clouds/status="
                                            + (simCfg.clouds > 0 ? "true" : "false"),
                                          "--prop:/sim/rendering/clouds3d-enable="
                                            + (simCfg.clouds > 1 ? "true" : "false"),
                                          "--prop:/controls/flight/auto-coordination="
                                            + (simCfg.autoCoord ? "true" : "false"),
                                          "--prop:/sim/frame-rate-throttle-hz="
                                            + [0, 20, 30, 60][simCfg.frameLimit],
                                          "--timeofday="
                                            + ["dawn", "morning", "noon", "afternoon",
                                               "dusk", "evening", "midnight"][simCfg.timeOfDay],
                                          // METAR from the net, or FlightGear's own
                                          // default weather; the option pair sets the
                                          // same property, so exactly one is passed
                                          simCfg.realWeather ? "--enable-real-weather-fetch"
                                                             : "--disable-real-weather-fetch" ]
                                        // In-flight scenery: FlightGear's TerraSync, pointed
                                        // straight at a mirror - its own server discovery is
                                        // a DNS NAPTR lookup that mobile resolvers refuse.
                                        .concat(simCfg.sceneryInFlight
                                                ? ["--enable-terrasync",
                                                   "--prop:/sim/terrasync/http-server=https://terrasync.eti.pg.gda.pl/ws2"]
                                                : ["--disable-terrasync"])
                                        // AI models only for a scenario: every AI object is
                                        // draw calls, and there is nothing to see without one.
                                        // --carrier takes precedence over --airport in
                                        // FlightGear (positioninit.cxx), so both can be passed.
                                        .concat(simCfg.scenario !== ""
                                                ? ["--enable-ai-models", "--ai-scenario=" + simCfg.scenario]
                                                  .concat(simCfg.scenarioCarrier !== ""
                                                          ? ["--carrier=" + simCfg.scenarioCarrier] : [])
                                                : ["--disable-ai-models"])
                                        // Visibility is not a property but an option that
                                        // feeds an environment preset, so it can only be
                                        // passed as --visibility, and only when it is meant
                                        // to override what the weather would have chosen.
                                        .concat(simCfg.visibility > 0
                                                ? ["--visibility="
                                                   + [0, 5000, 10000, 20000, 40000, 80000][simCfg.visibility]]
                                                : []),
                                        // Where the flight actually begins: on a
                                        // carrier that is the ship, not the airport
                                        // (--carrier wins over --airport), so the
                                        // scenery is fetched there.  Zero when the
                                        // stored coordinates belong to a different
                                        // airport: then nothing is fetched, rather
                                        // than the wrong region.
                                        simCfg.scenarioCarrier !== "" ? simCfg.scenarioLat
                                            : (simCfg.airportCoordsIcao === simCfg.airport
                                               ? simCfg.airportLat : 0),
                                        simCfg.scenarioCarrier !== "" ? simCfg.scenarioLon
                                            : (simCfg.airportCoordsIcao === simCfg.airport
                                               ? simCfg.airportLon : 0),
                                        simCfg.sceneryRefresh)
                        }
                    }
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Simulation settings")
                    onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Open cockpit")
                    enabled: rt !== null && rt.simRunning
                    onClicked: pageStack.push(Qt.resolvedUrl("FlightPage.qml"), { rt: rt })
                }

                // Starting the simulator goes straight to the cockpit.  The
                // page shows "waiting for fgfs" until the first frame
                // arrives, which is the same minute or two the start page
                // would have shown, only already in the right place.  The
                // button above stays for coming back after leaving.
                Connections {
                    target: rt
                    onStateChanged: {
                        if (!rt) return
                        if (rt.simRunning && !cockpitOpened) {
                            cockpitOpened = true
                            pageStack.push(Qt.resolvedUrl("FlightPage.qml"), { rt: rt })
                        } else if (!rt.simRunning) {
                            cockpitOpened = false
                        }
                    }
                }
            }
        }
    }
}
