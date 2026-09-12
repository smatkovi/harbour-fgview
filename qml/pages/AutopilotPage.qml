import QtQuick 2.6
import Sailfish.Silica 1.0

// The autopilot, as far as a phone can sensibly reach it.  Two of them
// really: FlightGear's own (the c172p, the Citation X and most others) and
// the A320 family's, which ignores FlightGear's properties and is driven
// through its own flight control unit.  ControlSender decides which, this
// page only shows what is there.
Page {
    id: page

    property var ctl                       // ControlSender

    // A value with two step sizes, which is what a knob on a real panel is
    Component {
        id: valueRow
        Row {
            property string title
            property int value
            property int small: 1
            property int large: 10
            property string unit
            property var apply                  // function(v)
            width: parent ? parent.width : 0
            spacing: Theme.paddingSmall

            Label {
                width: parent.width * 0.32
                anchors.verticalCenter: parent.verticalCenter
                text: title
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
            }
            FlatButton { text: "−" + large; onClicked: apply(value - large) }
            FlatButton { text: "−" + small; onClicked: apply(value - small) }
            Label {
                width: parent.width * 0.2
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                text: value + " " + unit
                font.pixelSize: Theme.fontSizeSmall
            }
            FlatButton { text: "+" + small; onClicked: apply(value + small) }
            FlatButton { text: "+" + large; onClicked: apply(value + large) }
        }
    }

    Component.onCompleted: if (ctl) ctl.apWatch(true)
    Component.onDestruction: if (ctl) ctl.apWatch(false)

    SilicaFlickable {
        anchors.fill: parent
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
            Loader {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                sourceComponent: valueRow
                onLoaded: {
                    item.title = qsTr("Speed")
                    item.unit = "kt"
                    item.small = 1
                    item.large = 10
                    item.apply = function(v) { ctl.apSetSpeed(v) }
                }
                Binding { target: item; property: "value"; value: ctl ? ctl.apSpeed : 0 }
            }

            SectionHeader { text: qsTr("Heading") }
            TextSwitch {
                text: qsTr("Hold heading")
                checked: ctl ? ctl.apHeadingOn : false
                onClicked: ctl.apHeadingHold(checked)
            }
            Loader {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                sourceComponent: valueRow
                onLoaded: {
                    item.title = qsTr("Heading")
                    item.unit = "°"
                    item.small = 1
                    item.large = 10
                    item.apply = function(v) { ctl.apSetHeading(v) }
                }
                Binding { target: item; property: "value"; value: ctl ? ctl.apHeading : 0 }
            }

            SectionHeader { text: qsTr("Altitude") }
            TextSwitch {
                text: qsTr("Hold altitude")
                checked: ctl ? ctl.apAltitudeOn : false
                onClicked: ctl.apAltitudeHold(checked)
            }
            Loader {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                sourceComponent: valueRow
                onLoaded: {
                    item.title = qsTr("Altitude")
                    item.unit = "ft"
                    item.small = 100
                    item.large = 1000
                    item.apply = function(v) { ctl.apSetAltitude(v) }
                }
                Binding { target: item; property: "value"; value: ctl ? ctl.apAltitude : 0 }
            }

            SectionHeader { text: qsTr("Climb and descent") }
            TextSwitch {
                text: qsTr("Hold vertical speed")
                description: qsTr("Instead of holding an altitude")
                checked: ctl ? ctl.apVerticalSpeedOn : false
                onClicked: ctl.apVerticalSpeedHold(checked)
            }
            Loader {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                sourceComponent: valueRow
                onLoaded: {
                    item.title = qsTr("Vertical speed")
                    item.unit = "fpm"
                    item.small = 100
                    item.large = 500
                    item.apply = function(v) { ctl.apSetVerticalSpeed(v) }
                }
                Binding { target: item; property: "value"; value: ctl ? ctl.apVerticalSpeed : 0 }
            }
        }

        VerticalScrollDecorator {}
    }
}
