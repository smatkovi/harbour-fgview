import QtQuick 2.6
import Sailfish.Silica 1.0

// Step two: large or small.  The split is by longest hard runway, 1800 m,
// which is roughly what a narrowbody airliner needs - so "large" is the
// list to pick from for a jet, "small" the one with the grass strips.
Page {
    id: page

    property var root                 // the country page, which carries the signal
    property string countryName: ""
    property string file: ""
    property int largeCount: 0
    property int smallCount: 0

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        PageHeader { title: page.countryName }

        ListItem {
            width: parent.width
            enabled: page.largeCount > 0
            contentHeight: Theme.itemSizeMedium
            Column {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    text: qsTr("Large airports")
                    color: parent.parent.enabled
                           ? (parent.parent.highlighted ? Theme.highlightColor
                                                        : Theme.primaryColor)
                           : Theme.secondaryColor
                }
                Label {
                    text: qsTr("%1 with a hard runway of 1800 m or more").arg(page.largeCount)
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("AirportListPage.qml"), {
                root: page.root, countryName: page.countryName,
                file: page.file, which: "large"
            })
        }

        ListItem {
            width: parent.width
            enabled: page.smallCount > 0
            contentHeight: Theme.itemSizeMedium
            Column {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    text: qsTr("Small airports")
                    color: parent.parent.enabled
                           ? (parent.parent.highlighted ? Theme.highlightColor
                                                        : Theme.primaryColor)
                           : Theme.secondaryColor
                }
                Label {
                    text: qsTr("%1 shorter or unpaved").arg(page.smallCount)
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("AirportListPage.qml"), {
                root: page.root, countryName: page.countryName,
                file: page.file, which: "small"
            })
        }
    }
}
