import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.fgview 1.0
import Nemo.Configuration 1.0

Page {
    id: page
    allowedOrientations: Orientation.Landscape
    backgroundColor: "black"
    // The rudder slider is a horizontal swipe along the bottom edge, which
    // is also the back gesture, so page navigation is off here.  There is
    // deliberately no back button either: the top of the screen is the
    // drag area for turning the view, and a button there would eat the
    // space and catch drags meant for the camera.  Sailfish's own gesture
    // for closing the application still works and is the way out.
    backNavigation: false
    showNavigationIndicator: false

    ControlSender { id: ctl }
    property var rt                        // FgRuntime, for a lesson's scenery

    // the settings the flight page acts on itself
    ConfigurationGroup {
        id: cfg
        path: "/apps/harbour-fgview/sim"
        property bool pauseInBackground: true
        property int  frameLimit: 0
    }

    // Minimised, or the screen off: freeze the simulation, so it neither
    // flies on unattended nor burns the battery drawing for nobody.
    //
    // A bound property rather than Connections on Qt.application: the
    // change signal of one's own property is certain to exist, and a
    // Connections handler whose signal name is wrong fails silently.
    property bool appActive: Qt.application.active
    onAppActiveChanged: {
        if (!cfg.pauseInBackground) return
        ctl.setPaused(!appActive, [0, 20, 30, 60][cfg.frameLimit])
    }

    // Switched off while paused: let it run again.
    Connections {
        target: cfg
        onPauseInBackgroundChanged: {
            if (!cfg.pauseInBackground && ctl.paused)
                ctl.setPaused(false, [0, 20, 30, 60][cfg.frameLimit])
        }
    }

    // Take the tilt reference once the page is up and the phone is in the
    // hand, not at the first sensor reading, which may still be the table.
    Timer {
        interval: 1200; running: true; repeat: false
        onTriggered: ctl.calibrate()
    }

    // ---- The rendered picture --------------------------------------

    FrameItem {
        id: frame
        anchors.fill: parent
    }

    // ---- Turning the view by dragging -------------------------------
    //
    // Where the view points, kept here so the drag and the look-to-the-side
    // buttons share one state: let a look button go and the view returns to
    // wherever the drag had left it, not bluntly to straight ahead.
    // Works in any view - the offsets belong to the current one, cockpit or
    // outside.
    property int viewHeading: 0
    property int viewPitch: 0

    // One finger turns the view, two change the field of view.
    //
    // Both in the same area on purpose.  A separate PinchArea underneath
    // does not work: whichever area is on top grabs the first touch point,
    // and the pinch is then never recognised, because it needs both.  That
    // is exactly how the two finger zoom broke when the drag was added.
    //
    // Declared before the controls, so throttle, rudder and buttons sit on
    // top and take their touch points first; what is left over lands here.
    MultiPointTouchArea {
        anchors.fill: parent
        maximumTouchPoints: 2
        mouseEnabled: true
        touchPoints: [ TouchPoint { id: p1 }, TouchPoint { id: p2 } ]

        // "drag" and "zoom" are exclusive, and once a zoom has begun it
        // holds until every finger is up: letting one finger go would
        // otherwise resume the drag from a position the finger never
        // travelled to, and the view would jump.
        property string mode: "none"

        property real startX: 0
        property real startY: 0
        property int startHeading: 0
        property int startPitch: 0
        // A screen width is half a turn: far enough to look over your
        // shoulder without letting go, fine enough to aim.
        property real degPerPixel: 180 / Math.max(1, page.width)

        property real startDist: 0
        property int startFov: 65
        property int lastFov: 65

        function bothDown() { return p1.pressed && p2.pressed }

        function gap() {
            var dx = p2.x - p1.x
            var dy = p2.y - p1.y
            return Math.max(1, Math.sqrt(dx * dx + dy * dy))
        }

        function beginZoom() {
            mode = "zoom"
            startDist = gap()
            startFov = lastFov
        }

        function beginDrag() {
            mode = "drag"
            startX = p1.x
            startY = p1.y
            startHeading = page.viewHeading
            startPitch = page.viewPitch
        }

        onPressed: {
            if (bothDown()) beginZoom()
            else if (mode === "none") beginDrag()
        }

        onUpdated: {
            if (bothDown()) {
                if (mode !== "zoom") beginZoom()
                // Fingers apart: look closer, so a smaller field of view.
                var fov = Math.max(20, Math.min(110, startFov * startDist / gap()))
                if (Math.round(fov) !== lastFov) {
                    lastFov = Math.round(fov)
                    ctl.setFieldOfView(lastFov)   // whole degrees only, so
                }                                 // telnet is not flooded
            } else if (mode === "drag") {
                // Drag right, look right.  Heading is counted
                // counterclockwise, so it decreases as the finger travels
                // right.
                var h = startHeading - (p1.x - startX) * degPerPixel
                var pi = startPitch - (p1.y - startY) * degPerPixel
                while (h > 180) h -= 360
                while (h < -180) h += 360
                // Stop short of straight up and down: past that the view
                // rolls over and the horizon ends up upside down.
                pi = Math.max(-80, Math.min(80, pi))
                page.viewHeading = Math.round(h)
                page.viewPitch = Math.round(pi)
                ctl.setViewOffsets(page.viewHeading, page.viewPitch)
            }
        }

        onReleased: {
            if (!p1.pressed && !p2.pressed) mode = "none"
        }
        onCanceled: mode = "none"
    }

    Label {
        anchors.centerIn: parent
        visible: !frame.connected
        color: Theme.secondaryHighlightColor
        text: qsTr("Waiting for fgfs\n(/dev/shm/fgfs-frame)")
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeLarge
    }

    // ---- Status line ------------------------------------------------

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
            text: "AIL " + ctl.aileron.toFixed(2) + "  ELV " + ctl.elevator.toFixed(2)
            color: ctl.tiltActive ? Theme.highlightColor : Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
    }

    // ---- Throttle, vertical on the left -----------------------------

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

            // MultiPointTouchArea, not MouseArea: a MouseArea only ever
            // gets the first touch point, so throttle and rudder could not
            // be moved at the same time - and neither could a held button.
            MultiPointTouchArea {
                anchors.fill: parent
                maximumTouchPoints: 1
                mouseEnabled: true
                touchPoints: [ TouchPoint { id: thrPoint } ]
                onPressed: setFromY(thrPoint.y)
                onUpdated: setFromY(thrPoint.y)
                function setFromY(y) {
                    ctl.throttle = Math.max(0, Math.min(1, 1 - y / height))
                }
            }
        }
    }

    // ---- Rudder, horizontal along the bottom ------------------------

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
                x: (parent.width - width) / 2 * (1 + ctl.rudder)
                radius: Theme.paddingSmall
                color: Theme.highlightColor
            }

            MultiPointTouchArea {
                anchors.fill: parent
                maximumTouchPoints: 1
                mouseEnabled: true
                touchPoints: [ TouchPoint { id: rudPoint } ]
                onPressed: setFromX(rudPoint.x)
                onUpdated: setFromX(rudPoint.x)
                onReleased: ctl.rudder = 0        // self-centring
                function setFromX(x) {
                    // FlightGear: +1 is right pedal, and right on the
                    // slider is right.  (The inverted reading that once
                    // suggested otherwise came from a broken packet.)
                    ctl.rudder = Math.max(-1, Math.min(1, (x / width) * 2 - 1))
                }
            }
        }
    }

    // ---- Switches, right --------------------------------------------

    // The current instruction of a running lesson: FlightGear shows it in
    // a PUI window, which this backend does not have, so it is read from
    // the property tree and shown here.
    Rectangle {
        id: lessonBox
        visible: ctl.tutorialRunning
        anchors { left: parent.left; right: buttonColumn.left; top: parent.top; margins: Theme.paddingMedium }
        height: lessonText.height + 2 * Theme.paddingMedium
        color: Theme.rgba(Theme.highlightDimmerColor, 0.8)
        radius: Theme.paddingSmall
        Label {
            id: lessonText
            anchors { left: parent.left; right: stopLesson.left; top: parent.top; margins: Theme.paddingMedium }
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.primaryColor
            text: ctl.tutorialMessage
        }
        FlatButton {
            id: stopLesson
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Theme.paddingSmall }
            width: Theme.itemSizeMedium
            text: "✕"
            onClicked: ctl.stopTutorial()
        }

    }

    // paused in the background: say so when the picture comes back
    Label {
        visible: ctl.paused
        anchors.centerIn: parent
        text: qsTr("Paused")
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeHuge
    }

    Column {
        id: buttonColumn
        width: Theme.itemSizeExtraLarge
        spacing: Theme.paddingSmall
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
            margins: Theme.paddingMedium
        }

        // Look to the side: arrows only, and in the control column at the
        // edge rather than over the middle of the picture, where they took
        // up viewport and caught drags meant for the camera.
        //
        // Held, not toggled: let go and the view comes back by itself, the
        // way you glance out of a window.  goal-heading-offset lets
        // FlightGear swing the view rather than snap it.
        Row {
            width: parent.width
            spacing: Theme.paddingSmall

            FlatButton {
                width: (parent.width - Theme.paddingSmall) / 2
                text: "◀"
                onPressedChanged: pressed ? ctl.setViewOffsets(90, page.viewPitch)
                                          : ctl.setViewOffsets(page.viewHeading, page.viewPitch)
            }
            FlatButton {
                width: (parent.width - Theme.paddingSmall) / 2
                text: "▶"
                onPressedChanged: pressed ? ctl.setViewOffsets(-90, page.viewPitch)
                                          : ctl.setViewOffsets(page.viewHeading, page.viewPitch)
            }
        }

        // Lessons on a long press rather than a button of their own: the
        // column was full, and a ninth row did not fit on the screen.
        FlatButton {
            width: parent.width
            text: qsTr("View")
            color: ctl.tutorialRunning ? Theme.highlightColor : Theme.primaryColor
            onClicked: ctl.cycleView()
            onPressAndHold: pageStack.push(Qt.resolvedUrl("LessonsPage.qml"),
                                           { ctl: ctl, rt: page.rt })
        }

        FlatButton {
            width: parent.width
            text: ctl.cranking ? qsTr("Cranking...")
                               : (ctl.engineOn ? qsTr("Engine off") : qsTr("Engine on"))
            color: ctl.engineOn ? Theme.highlightColor : Theme.primaryColor
            enabled: !ctl.cranking
            onClicked: ctl.engineOn ? ctl.stopEngine() : ctl.startEngine()
        }

        FlatButton {
            width: parent.width
            text: ctl.tiltActive ? qsTr("Tilt on") : qsTr("Tilt off")
            color: ctl.tiltActive ? Theme.highlightColor : Theme.primaryColor
            onClicked: {
                if (!ctl.tiltActive) ctl.calibrate()
                ctl.tiltActive = !ctl.tiltActive
            }
        }

        FlatButton {
            width: parent.width
            text: qsTr("Zero")
            enabled: ctl.tiltActive
            onClicked: ctl.calibrate()
        }

        FlatButton {
            width: parent.width
            text: ctl.gearDown ? qsTr("Gear up") : qsTr("Gear down")
            onClicked: ctl.gearDown = !ctl.gearDown
        }

        FlatButton {
            width: parent.width
            // The c172p's detents: up, 10, 20, 30 degrees.  Half steps sit
            // between them and the lever settles on whichever is nearer,
            // which made the flaps look stuck at the first notch.
            property var detents: [0.0, 1/3, 2/3, 1.0]
            property var degrees: [0, 10, 20, 30]
            function nearest() {
                var best = 0
                for (var i = 1; i < detents.length; ++i)
                    if (Math.abs(detents[i] - ctl.flaps) < Math.abs(detents[best] - ctl.flaps))
                        best = i
                return best
            }
            text: qsTr("Flaps") + " " + degrees[nearest()] + "°"
            onClicked: ctl.flaps = detents[(nearest() + 1) % detents.length]
            onPressAndHold: ctl.flaps = 0.0   // fully up in one go
        }

        FlatButton {
            width: parent.width
            text: qsTr("Brake")
            down: ctl.brake > 0.5
            onPressedChanged: ctl.brake = pressed ? 1.0 : 0.0
        }
    }
}
