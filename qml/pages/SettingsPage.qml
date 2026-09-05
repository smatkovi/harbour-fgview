import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

// Simulation settings.  The defaults are what measured best on the Jolla
// Phone 2026; each one trades picture for frame time and is a property
// FlightGear reads at start.
Page {
    id: page

    ConfigurationGroup {
        id: cfg
        path: "/apps/harbour-fgview/sim"
        property real vegetation: 0.0
        property int  modelHz: 60
        property bool traffic: false
        property int  detailRange: 1500
        property int  filtering: 1
        property bool particles: true
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Simulation") }

            Slider {
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
                text: qsTr("Trees are the largest single draw cost. 30 % still reads as forest and takes a third off the frame time.")
            }

            ComboBox {
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
                text: qsTr("AI traffic")
                description: qsTr("Scheduled airline traffic. Fifty aircraft at Vienna, each with motion, model and draw calls. FlightGear 2020.3 cannot cap the number.")
                checked: cfg.traffic
                onCheckedChanged: cfg.traffic = checked
            }

            Slider {
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

            TextSwitch {
                text: qsTr("Particles")
                description: qsTr("Smoke, dust, spray. About 1.5 ms.")
                checked: cfg.particles
                onCheckedChanged: cfg.particles = checked
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
