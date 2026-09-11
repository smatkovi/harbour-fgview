import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

// Simulation settings.
//
// Every default here is what the simulator already did before the setting
// existed, so opening this page and closing it again changes nothing.  The
// ones that were measured on this device say so in their description; the
// rest are here because they visibly change the picture or the handling.
//
// Only settings that make sense on a phone are offered.  FlightGear has
// several hundred properties, and most of them either need a keyboard, need
// a mouse, or would quietly make the frame time worse.
Page {
    id: page

    // One place for the defaults, so the reset button and the initial
    // values cannot drift apart.
    readonly property var defaults: ({
        vegetation:   0.0,
        buildings:    false,     // FlightGear's own default
        detailRange:  1500,
        visibility:   0,         // 0 = leave it to the weather
        filtering:    1,
        clouds:       2,         // 0 none, 1 flat, 2 volumetric (FG default)
        timeOfDay:    2,         // noon, which is what used to be hardcoded
        trafficLevel: 0,
        modelHz:      60,
        autoCoord:    false,     // FlightGear's own default
        frameLimit:   0,         // 0 = uncapped, FlightGear's own default
        particles:    true,
        sound:        false,
        pauseInBackground: true, // freeze the simulation while the app is not on screen
        sceneryRefresh: false,   // check offline, fetch only what is missing
        sceneryInFlight: false,  // FlightGear's TerraSync: tiles around the aircraft
        realWeather:  false      // METAR from the net; needs a connection in flight
    })

    ConfigurationGroup {
        id: cfg
        path: "/apps/harbour-fgview/sim"
        property real vegetation: 0.0
        property bool buildings: false
        property int  detailRange: 1500
        property int  visibility: 0
        property int  filtering: 1
        property int  clouds: 2
        property int  timeOfDay: 2
        property int  trafficLevel: 0
        property int  modelHz: 60
        property bool autoCoord: false
        property int  frameLimit: 0
        property bool particles: true
        property bool sound: false
        property bool sceneryRefresh: false
        property bool sceneryInFlight: false
        property bool realWeather: false
        property bool pauseInBackground: true
    }

    // The controls are written to as well as read from, so a reset has to
    // set both sides: a slider that the user has dragged no longer follows
    // its binding, and would otherwise keep showing the old value.
    function resetToDefaults() {
        var d = page.defaults
        cfg.vegetation = d.vegetation;   cfg.buildings = d.buildings
        cfg.sceneryRefresh = d.sceneryRefresh
        cfg.pauseInBackground = d.pauseInBackground
        cfg.sceneryInFlight = d.sceneryInFlight
        cfg.realWeather = d.realWeather
        cfg.detailRange = d.detailRange; cfg.visibility = d.visibility
        cfg.filtering = d.filtering;     cfg.clouds = d.clouds
        cfg.timeOfDay = d.timeOfDay;     cfg.trafficLevel = d.trafficLevel
        cfg.modelHz = d.modelHz;         cfg.autoCoord = d.autoCoord
        cfg.frameLimit = d.frameLimit;   cfg.particles = d.particles
        cfg.sound = d.sound

        treeSlider.value        = d.vegetation

        sceneryRefreshSwitch.checked = d.sceneryRefresh
        pauseSwitch.checked = d.pauseInBackground
        sceneryInFlightSwitch.checked = d.sceneryInFlight
        realWeatherSwitch.checked = d.realWeather
        buildingSwitch.checked  = d.buildings
        rangeSlider.value       = d.detailRange
        visBox.currentIndex     = d.visibility
        filterBox.currentIndex  = d.filtering >= 8 ? 2 : (d.filtering >= 4 ? 1 : 0)
        cloudBox.currentIndex   = d.clouds
        todBox.currentIndex     = d.timeOfDay
        trafficBox.currentIndex = d.trafficLevel
        modelBox.currentIndex   = d.modelHz === 120 ? 1 : 0
        coordSwitch.checked     = d.autoCoord
        limitBox.currentIndex   = d.frameLimit
        particleSwitch.checked  = d.particles
        soundSwitch.checked     = d.sound
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        PullDownMenu {
            MenuItem {
                text: qsTr("Reset to defaults")
                onClicked: page.resetToDefaults()
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Simulation") }

            // ---- Scenery ------------------------------------------------
            SectionHeader { text: qsTr("Application") }

            TextSwitch {
                id: pauseSwitch
                text: qsTr("Pause when in the background")
                description: qsTr("Freezes the flight and the clock while the app is minimised or the screen is off, and throttles the simulator to two frames a second so it stops eating the battery. Resumes when the app comes back.")
                checked: cfg.pauseInBackground
                onCheckedChanged: cfg.pauseInBackground = checked
            }

            SectionHeader { text: qsTr("Scenery") }

            Slider {
                id: treeSlider
                width: parent.width
                label: qsTr("Trees")
                minimumValue: 0; maximumValue: 1; stepSize: 0.1
                value: cfg.vegetation
                valueText: Math.round(value * 100) + " %"
                onReleased: cfg.vegetation = value
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("The largest single cost: about 60 % of everything drawn in a frame. Off, this measured 68.5 → 40.8 ms per frame at Vienna. 30 % still reads as forest.")
            }

            TextSwitch {
                id: sceneryInFlightSwitch
                text: qsTr("Update scenery in flight")
                description: qsTr("FlightGear's own TerraSync: while you fly, the tiles around the aircraft are fetched from a mirror and loaded as they arrive, so leaving the pre-fetched area does not end over water. Also fetches the shared models and airport data once (a few hundred MB). Needs a connection while flying.")
                checked: cfg.sceneryInFlight
                onCheckedChanged: cfg.sceneryInFlight = checked
            }

            TextSwitch {
                id: sceneryRefreshSwitch
                text: qsTr("Refresh scenery on every start")
                description: qsTr("Normally the scenery around the departure airport is checked offline and only fetched when a finished download does not cover it. On, every start compares it with the TerraSync servers and fetches what changed.")
                checked: cfg.sceneryRefresh
                onCheckedChanged: cfg.sceneryRefresh = checked
            }

            TextSwitch {
                id: buildingSwitch
                text: qsTr("Random buildings")
                description: qsTr("Generated houses where the scenery has no modelled ones. Off by default, as in FlightGear itself.")
                checked: cfg.buildings
                onCheckedChanged: cfg.buildings = checked
            }

            Slider {
                id: rangeSlider
                width: parent.width
                label: qsTr("Detail range")
                minimumValue: 500; maximumValue: 3000; stepSize: 250
                value: cfg.detailRange
                valueText: value + " m"
                onReleased: cfg.detailRange = value
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("How far out the detailed terrain objects are drawn.")
            }

            ComboBox {
                id: visBox
                width: parent.width
                label: qsTr("Visibility")
                currentIndex: cfg.visibility
                menu: ContextMenu {
                    MenuItem { text: qsTr("As the weather has it") }
                    MenuItem { text: "5 km" }
                    MenuItem { text: "10 km" }
                    MenuItem { text: "20 km" }
                    MenuItem { text: "40 km" }
                    MenuItem { text: "80 km" }
                }
                onCurrentIndexChanged: cfg.visibility = currentIndex
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("How far you can see. Less means fewer terrain tiles to load and to draw; more is prettier and costs.")
            }

            ComboBox {
                id: filterBox
                width: parent.width
                label: qsTr("Texture filtering")
                currentIndex: cfg.filtering >= 8 ? 2 : (cfg.filtering >= 4 ? 1 : 0)
                menu: ContextMenu {
                    MenuItem { text: qsTr("off") }
                    MenuItem { text: "4x" }
                    MenuItem { text: "8x" }
                }
                onCurrentIndexChanged: cfg.filtering = currentIndex === 2 ? 8 : (currentIndex === 1 ? 4 : 1)
            }

            // ---- Weather and time ---------------------------------------
            SectionHeader { text: qsTr("Weather and time") }

            TextSwitch {
                id: realWeatherSwitch
                text: qsTr("Real weather")
                description: qsTr("Fetches the current METAR for the area at start and keeps updating it in flight, so wind, clouds, visibility and pressure are what the nearest station reports. Needs a network connection while flying; a visibility set below overrides it.")
                checked: cfg.realWeather
                onCheckedChanged: cfg.realWeather = checked
            }

            ComboBox {
                id: cloudBox
                width: parent.width
                label: qsTr("Clouds")
                currentIndex: cfg.clouds
                menu: ContextMenu {
                    MenuItem { text: qsTr("None") }
                    MenuItem { text: qsTr("Flat layers") }
                    MenuItem { text: qsTr("Volumetric") }
                }
                onCurrentIndexChanged: cfg.clouds = currentIndex
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Measured: off saves 2.4 ms per frame. Clouds are only about 5 % of what is drawn, so this is a look-and-feel choice rather than a performance one.")
            }

            ComboBox {
                id: todBox
                width: parent.width
                label: qsTr("Time of day")
                currentIndex: cfg.timeOfDay
                menu: ContextMenu {
                    MenuItem { text: qsTr("Dawn") }
                    MenuItem { text: qsTr("Morning") }
                    MenuItem { text: qsTr("Noon") }
                    MenuItem { text: qsTr("Afternoon") }
                    MenuItem { text: qsTr("Dusk") }
                    MenuItem { text: qsTr("Evening") }
                    MenuItem { text: qsTr("Midnight") }
                }
                onCurrentIndexChanged: cfg.timeOfDay = currentIndex
            }

            // ---- Traffic ------------------------------------------------
            SectionHeader { text: qsTr("Traffic") }

            ComboBox {
                id: trafficBox
                width: parent.width
                label: qsTr("AI traffic")
                currentIndex: cfg.trafficLevel
                menu: ContextMenu {
                    MenuItem { text: qsTr("Off") }
                    MenuItem { text: qsTr("A few (12 %)") }
                    MenuItem { text: qsTr("Some (25 %)") }
                    MenuItem { text: qsTr("Half (50 %)") }
                    MenuItem { text: qsTr("Most (75 %)") }
                    MenuItem { text: qsTr("All") }
                }
                onCurrentIndexChanged: cfg.trafficLevel = currentIndex
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Scheduled airline traffic; fifty aircraft at Vienna, each with motion, model and draw calls. FlightGear tests the share against a random value that can only take eight values, so these are the percentages actually reachable.")
            }

            // ---- Flying -------------------------------------------------
            SectionHeader { text: qsTr("Flying") }

            ComboBox {
                id: modelBox
                width: parent.width
                label: qsTr("Flight model rate")
                currentIndex: cfg.modelHz === 120 ? 1 : 0
                menu: ContextMenu {
                    MenuItem { text: "60 Hz" }
                    MenuItem { text: "120 Hz" }
                }
                onCurrentIndexChanged: cfg.modelHz = currentIndex === 1 ? 120 : 60
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("60 Hz is plenty for a light aircraft and halves the flight model's share. 120 Hz for aerobatics or helicopters.")
            }

            TextSwitch {
                id: coordSwitch
                text: qsTr("Auto-coordination")
                description: qsTr("Moves the rudder with the ailerons, so turns stay coordinated without reaching for the bottom slider.")
                checked: cfg.autoCoord
                onCheckedChanged: cfg.autoCoord = checked
            }

            // ---- System -------------------------------------------------
            SectionHeader { text: qsTr("System") }

            ComboBox {
                id: limitBox
                width: parent.width
                label: qsTr("Frame rate limit")
                currentIndex: cfg.frameLimit
                menu: ContextMenu {
                    MenuItem { text: qsTr("None") }
                    MenuItem { text: "20 Hz" }
                    MenuItem { text: "30 Hz" }
                    MenuItem { text: "60 Hz" }
                }
                onCurrentIndexChanged: cfg.frameLimit = currentIndex
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("A cap costs nothing while the simulator is slower than it anyway, and saves battery and heat once it is faster.")
            }

            TextSwitch {
                id: particleSwitch
                text: qsTr("Particles")
                description: qsTr("Smoke, dust, spray. About 1.5 ms.")
                checked: cfg.particles
                onCheckedChanged: cfg.particles = checked
            }

            TextSwitch {
                id: soundSwitch
                text: qsTr("Sound")
                description: qsTr("Engine, wind and warnings, through the phone's audio hardware. Measured at 2.7 ms per frame.")
                checked: cfg.sound
                onCheckedChanged: cfg.sound = checked
            }

            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Takes effect at the next start of the simulator.")
            }
        }
    }
}
