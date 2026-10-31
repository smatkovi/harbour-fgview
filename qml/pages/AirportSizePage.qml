import QtQuick 2.6
import Sailfish.Silica 1.0

// Step two: which kind.  By the runways, see make-airports.py: a hard runway
// of 1800 m or more is roughly what a narrowbody airliner needs, so "large"
// is the list to pick from for a jet; "small" has a shorter hard runway;
// airfields have grass, gravel or dirt only - the strips for a Cub or a
// glider.
Page {
    id: page

    property var root                 // the country page, which carries the signal
    property string countryName: ""
    property string file: ""
    property int largeCount: 0
    property int smallCount: 0
    property int fieldCount: 0

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        PageHeader { title: page.countryName }

        Repeater {
            model: [
                { which: "large", count: page.largeCount, title: qsTr("Large airports"),
                  detail: qsTr("%1 with a hard runway of 1800 m or more").arg(page.largeCount) },
                { which: "small", count: page.smallCount, title: qsTr("Small airports"),
                  detail: qsTr("%1 with a shorter hard runway").arg(page.smallCount) },
                { which: "field", count: page.fieldCount, title: qsTr("Airfields"),
                  detail: qsTr("%1 with grass, gravel or dirt runways only").arg(page.fieldCount) }
            ]

            ListItem {
                id: item
                width: page.width
                enabled: modelData.count > 0
                contentHeight: Theme.itemSizeMedium
                Column {
                    anchors {
                        left: parent.left; leftMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: modelData.title
                        color: item.enabled ? (item.highlighted ? Theme.highlightColor
                                                                : Theme.primaryColor)
                                            : Theme.secondaryColor
                    }
                    Label {
                        text: modelData.detail
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeExtraSmall
                    }
                }
                onClicked: pageStack.push(Qt.resolvedUrl("AirportListPage.qml"), {
                    root: page.root, countryName: page.countryName,
                    file: page.file, which: modelData.which
                })
            }
        }
    }
}
