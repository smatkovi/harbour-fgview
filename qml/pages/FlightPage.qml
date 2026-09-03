import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.fgview 1.0

Page {
    id: page
    allowedOrientations: Orientation.Landscape
    backgroundColor: "black"
    // The rudder slider is a horizontal swipe along the bottom edge, which
    // is also the back gesture.  Leaving the page is the button's job.
    backNavigation: false
    showNavigationIndicator: false

    ControlSender { id: ctl }

    // ---- Das gerenderte Bild --------------------------------------

    FrameItem {
        id: frame
        anchors.fill: parent
    }

    Label {
        anchors.centerIn: parent
        visible: !frame.connected
        color: Theme.secondaryHighlightColor
        text: "Warte auf fgfs\n(/dev/shm/fgfs-frame)"
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeLarge
    }

    // ---- Statuszeile ----------------------------------------------

    Row {
        anchors {
            top: parent.top
            left: parent.left
            margins: Theme.paddingMedium
        }
        spacing: Theme.paddingLarge
        opacity: 0.75

        Label {
            text: frame.fps + " fps"
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
        Label {
            text: "QR " + ctl.aileron.toFixed(2) + "  HR " + ctl.elevator.toFixed(2)
            color: ctl.tiltActive ? Theme.highlightColor : Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
    }

    // ---- Gashebel, links senkrecht --------------------------------

    Item {
        id: throttleBox
        width: Theme.itemSizeMedium
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            topMargin: Theme.itemSizeSmall
            bottomMargin: Theme.itemSizeSmall
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.paddingMedium
            radius: Theme.paddingSmall
            color: Theme.rgba(Theme.highlightBackgroundColor, 0.15)
            border.color: Theme.rgba(Theme.highlightColor, 0.4)
            border.width: 1

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.margins: 2
                height: (parent.height - 4) * ctl.throttle
                radius: Theme.paddingSmall
                color: Theme.rgba(Theme.highlightColor, 0.55)
            }

            Label {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.top }
                text: Math.round(ctl.throttle * 100) + "%"
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            MouseArea {
                anchors.fill: parent
                onPositionChanged: setFromY(mouse.y)
                onPressed: setFromY(mouse.y)
                function setFromY(y) {
                    ctl.throttle = Math.max(0, Math.min(1, 1 - y / height))
                }
            }
        }
    }

    // ---- Seitenruder, unten waagrecht ------------------------------

    Item {
        height: Theme.itemSizeSmall
        anchors {
            bottom: parent.bottom
            left: throttleBox.right
            right: buttonColumn.left
            margins: Theme.paddingMedium
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.paddingSmall
            color: Theme.rgba(Theme.highlightBackgroundColor, 0.15)
            border.color: Theme.rgba(Theme.highlightColor, 0.4)
            border.width: 1

            Rectangle {
                width: 3
                height: parent.height
                x: parent.width / 2 - 1.5
                color: Theme.rgba(Theme.secondaryColor, 0.5)
            }

            Rectangle {
                width: Theme.paddingLarge
                height: parent.height - 4
                y: 2
                x: (parent.width - width) / 2 * (1 - ctl.rudder)
                radius: Theme.paddingSmall
                color: Theme.highlightColor
            }

            MouseArea {
                anchors.fill: parent
                onPositionChanged: setFromX(mouse.x)
                onPressed: setFromX(mouse.x)
                onReleased: ctl.rudder = 0        // selbstzentrierend
                function setFromX(x) {
                    // FlightGear: +1 is right pedal.  The slider reads
                    // right-to-left in this orientation, hence the sign.
                    ctl.rudder = -Math.max(-1, Math.min(1, (x / width) * 2 - 1))
                }
            }
        }
    }

    // ---- Schalter, rechts ------------------------------------------

    Column {
        id: buttonColumn
        width: Theme.itemSizeExtraLarge
        spacing: Theme.paddingSmall
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
            margins: Theme.paddingMedium
        }

        Button {
            width: parent.width
            text: "Sicht"
            onClicked: ctl.cycleView()
        }

        Button {
            width: parent.width
            text: ctl.cranking ? "Anlasser..." : (ctl.engineOn ? "Motor aus" : "Motor an")
            color: ctl.engineOn ? Theme.highlightColor : Theme.primaryColor
            enabled: !ctl.cranking
            onClicked: ctl.engineOn ? ctl.stopEngine() : ctl.startEngine()
        }

        Button {
            width: parent.width
            text: ctl.tiltActive ? "Neigung an" : "Neigung aus"
            color: ctl.tiltActive ? Theme.highlightColor : Theme.primaryColor
            onClicked: {
                if (!ctl.tiltActive) ctl.calibrate()
                ctl.tiltActive = !ctl.tiltActive
            }
        }

        Button {
            width: parent.width
            text: "Nullen"
            enabled: ctl.tiltActive
            onClicked: ctl.calibrate()
        }

        Button {
            width: parent.width
            text: ctl.gearDown ? "Fahrwerk aus" : "Fahrwerk ein"
            onClicked: ctl.gearDown = !ctl.gearDown
        }

        Button {
            width: parent.width
            text: "Klappen " + Math.round(ctl.flaps * 100) + "%"
            onClicked: ctl.flaps = ctl.flaps >= 1.0 ? 0.0 : ctl.flaps + 0.5
        }

        Button {
            width: parent.width
            text: "Bremse"
            down: ctl.brake > 0.5
            onPressedChanged: ctl.brake = pressed ? 1.0 : 0.0
        }
    }
}
