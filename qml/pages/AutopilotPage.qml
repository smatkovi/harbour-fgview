import QtQuick 2.6
import Sailfish.Silica 1.0

// The autopilot, as far as a phone can sensibly reach it.  Two of them
// really: FlightGear's own (the c172p, the Citation X and most others) and
// the A320 family's, which ignores FlightGear's properties and is driven
// through its own flight control unit.  ControlSender decides which, this
// page only shows what is there.
//
// The map sits on the left, the panel on the right: the page is landscape,
// like the cockpit it comes from.
Page {
    id: page

    property var ctl                       // ControlSender

    // polls the autopilot state and the position for the map
    Component.onCompleted: if (ctl) ctl.apWatch(true)
    Component.onDestruction: if (ctl) ctl.apWatch(false)

    MapView {
        id: map
        ctl: page.ctl
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: Math.min(parent.height, parent.width * 0.5)
    }

    SilicaFlickable {
        anchors { left: map.right; right: parent.right; top: parent.top; bottom: parent.bottom }
        clip: true
        contentHeight: col.height + Theme.paddingLarge

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Autopilot")
                description: ctl && ctl.apKind === "a320" ? qsTr("A320 flight control unit")
                           : ctl && ctl.apKind === "generic" ? qsTr("FlightGear autopilot")
                           : qsTr("asking the simulator…")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: ctl ? qsTr("Active: %1").arg(ctl.apModes !== "" ? ctl.apModes : "–") : ""
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
            }

            // ---- the one tap that does the useful thing ---------------
            Row {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                FlatButton {
                    text: qsTr("Hold this")
                    width: (parent.width - 2 * Theme.paddingMedium) / 3
                    onClicked: ctl.apHoldCurrent()
                }
                FlatButton {
                    text: qsTr("Wings level")
                    width: (parent.width - 2 * Theme.paddingMedium) / 3
                    onClicked: ctl.apWingsLevel()
                }
                FlatButton {
                    text: qsTr("All off")
                    width: (parent.width - 2 * Theme.paddingMedium) / 3
                    onClicked: ctl.apAllOff()
                }
            }

            TextSwitch {
                text: qsTr("Autopilot")
                description: qsTr("Flies what is switched on below")
                checked: ctl ? ctl.apOn : false
                onClicked: ctl.apMaster(checked)
            }

            TextSwitch {
                text: qsTr("Autothrust")
                description: qsTr("Holds the speed with the throttle")
                checked: ctl ? ctl.apAutothrustOn : false
                onClicked: ctl.apSpeedHold(checked)
            }

            SectionHeader { text: qsTr("Speed") }
            ApValueRow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                title: qsTr("Speed")
                unit: "kt"
                small: 1
                large: 10
                value: ctl ? ctl.apSpeed : 0
                onChangeTo: ctl.apSetSpeed(v)
            }

            SectionHeader { text: qsTr("Heading") }
            TextSwitch {
                text: qsTr("Hold heading")
                checked: ctl ? ctl.apHeadingOn : false
                onClicked: ctl.apHeadingHold(checked)
            }
            ApValueRow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                title: qsTr("Heading")
                unit: "°"
                small: 1
                large: 10
                value: ctl ? ctl.apHeading : 0
                onChangeTo: ctl.apSetHeading(v)
            }

            SectionHeader { text: qsTr("Altitude") }
            TextSwitch {
                text: qsTr("Hold altitude")
                checked: ctl ? ctl.apAltitudeOn : false
                onClicked: ctl.apAltitudeHold(checked)
            }
            ApValueRow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                title: qsTr("Altitude")
                unit: "ft"
                small: 100
                large: 1000
                value: ctl ? ctl.apAltitude : 0
                onChangeTo: ctl.apSetAltitude(v)
            }

            SectionHeader { text: qsTr("Climb and descent") }
            TextSwitch {
                text: qsTr("Hold vertical speed")
                description: qsTr("Instead of holding an altitude")
                checked: ctl ? ctl.apVerticalSpeedOn : false
                onClicked: ctl.apVerticalSpeedHold(checked)
            }
            ApValueRow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                title: qsTr("Vertical speed")
                unit: "fpm"
                small: 100
                large: 500
                value: ctl ? ctl.apVerticalSpeed : 0
                onChangeTo: ctl.apSetVerticalSpeed(v)
            }
        }

        VerticalScrollDecorator {}
    }
}
