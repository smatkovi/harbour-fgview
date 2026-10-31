import QtQuick 2.6
import Sailfish.Silica 1.0

// One autopilot value: two step sizes either way, which is what a knob on a
// real panel is, and the value itself as a field to type an exact one into.
//
// The buttons get their width here.  FlatButton is a plain Item with no
// implicit width, and in the Row this replaces they had none: zero wide,
// they could be neither seen nor tapped, so no value could be changed.
Item {
    id: row

    property string title
    property string unit
    property int value
    property int small: 1
    property int large: 10

    // the value the user asked for; clamping and wrapping are ControlSender's
    signal changeTo(int v)

    width: parent ? parent.width : 0
    height: Theme.itemSizeSmall

    readonly property real titleWidth: width * 0.24
    readonly property real fieldWidth: width * 0.2
    readonly property real buttonWidth: (width - titleWidth - fieldWidth - 5 * Theme.paddingSmall) / 4

    Row {
        anchors.fill: parent
        spacing: Theme.paddingSmall

        Label {
            width: row.titleWidth
            anchors.verticalCenter: parent.verticalCenter
            text: row.title
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeSmall
            truncationMode: TruncationMode.Fade
        }
        FlatButton {
            width: row.buttonWidth
            anchors.verticalCenter: parent.verticalCenter
            text: "−" + row.large
            onClicked: row.changeTo(row.value - row.large)
        }
        FlatButton {
            width: row.buttonWidth
            anchors.verticalCenter: parent.verticalCenter
            text: "−" + row.small
            onClicked: row.changeTo(row.value - row.small)
        }
        TextField {
            id: field
            width: row.fieldWidth
            anchors.verticalCenter: parent.verticalCenter
            textLeftMargin: 0
            textRightMargin: 0
            labelVisible: false
            horizontalAlignment: TextInput.AlignHCenter
            font.pixelSize: Theme.fontSizeSmall
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            placeholderText: row.unit
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: {
                var v = parseInt(text, 10)
                if (!isNaN(v)) row.changeTo(v)
                focus = false
            }
            // Follows the simulator while nobody is typing.  Tapped, it holds
            // the bare number, selected, so typing replaces it; a value typed
            // and not confirmed goes back to what is set.
            onActiveFocusChanged: {
                if (activeFocus) {
                    text = String(row.value)
                    selectAll()
                } else {
                    text = row.value + " " + row.unit
                }
            }
        }
        Binding {
            target: field
            property: "text"
            value: row.value + " " + row.unit
            when: !field.activeFocus
        }
        FlatButton {
            width: row.buttonWidth
            anchors.verticalCenter: parent.verticalCenter
            text: "+" + row.small
            onClicked: row.changeTo(row.value + row.small)
        }
        FlatButton {
            width: row.buttonWidth
            anchors.verticalCenter: parent.verticalCenter
            text: "+" + row.large
            onClicked: row.changeTo(row.value + row.large)
        }
    }
}
