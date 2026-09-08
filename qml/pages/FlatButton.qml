import QtQuick 2.6
import Sailfish.Silica 1.0

// Silica's Button puts its label on one line and clips what does not fit,
// which is too little for the captions in the flight page's narrow column.
// Same idea, but the label wraps and the item grows with it.
BackgroundItem {
    id: root

    property string text
    property color color: Theme.primaryColor
    property bool down: false

    height: Math.max(Theme.itemSizeSmall, label.implicitHeight + 2 * Theme.paddingMedium)

    Rectangle {
        anchors.fill: parent
        radius: Theme.paddingSmall
        color: root.down || root.highlighted
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
}
