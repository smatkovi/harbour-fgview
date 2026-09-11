import QtQuick 2.6
import Sailfish.Silica 1.0

// Silica's Button puts its label on one line and clips what does not fit,
// which is too little for the captions in the flight page's narrow column.
// Same idea, but the label wraps and the item grows with it.
//
// Built on MultiPointTouchArea rather than BackgroundItem/MouseArea.  A
// MouseArea only ever sees the first touch point, so with mouse-based
// buttons the cockpit could take exactly one input at a time: holding the
// brake and opening the throttle, or looking left while steering, was not
// possible.  Each MultiPointTouchArea takes its own point, so every control
// on the flight page can be used at once.
//
// The API is the one the previous version had: text, color, down, enabled,
// pressed, clicked(), pressAndHold().
Item {
    id: root

    property string text
    property color color: Theme.primaryColor
    property bool down: false
    // No "property bool enabled" here: Item already has one, and
    // redeclaring it is a duplicate-property error that would stop the
    // whole page from loading.  The callers' enabled: bindings land on
    // Item's own property, which is what they always did.
    //
    // Read by callers that show a held state, e.g. the brake.
    property bool pressed: touch.isDown && root.enabled

    signal clicked()
    signal pressAndHold()

    height: Math.max(Theme.itemSizeSmall, label.implicitHeight + 2 * Theme.paddingMedium)

    Rectangle {
        anchors.fill: parent
        radius: Theme.paddingSmall
        color: root.down || root.pressed
               ? Theme.rgba(Theme.highlightBackgroundColor, Theme.highlightBackgroundOpacity)
               : Theme.rgba(Theme.primaryColor, 0.1)
    }

    Label {
        id: label
        anchors {
            left: parent.left; right: parent.right
            verticalCenter: parent.verticalCenter
            margins: Theme.paddingSmall
        }
        text: root.text
        color: root.enabled ? root.color : Theme.secondaryColor
        font.pixelSize: Theme.fontSizeExtraSmall
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }

    MultiPointTouchArea {
        id: touch
        anchors.fill: parent
        maximumTouchPoints: 1
        mouseEnabled: true          // so it still works with a mouse/emulator

        // Not "pressed": MultiPointTouchArea already has a signal of that
        // name, and a property would collide with it.
        property bool isDown: false
        property bool held: false

        onPressed: {
            if (!root.enabled) return
            isDown = true
            held = false
            holdTimer.restart()
        }
        onReleased: {
            if (!isDown) return
            holdTimer.stop()
            isDown = false
            // A press that already fired pressAndHold does not also click:
            // otherwise holding the flap button to retract would then also
            // step it one notch on release.
            if (!held && root.enabled) root.clicked()
            held = false
        }
        onCanceled: {
            holdTimer.stop()
            isDown = false
            held = false
        }
    }

    Timer {
        id: holdTimer
        interval: 800
        onTriggered: {
            if (touch.isDown && root.enabled) {
                touch.held = true
                root.pressAndHold()
            }
        }
    }
}
