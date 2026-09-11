import QtQuick 2.6
import Sailfish.Silica 1.0

// Step four: the runway.  "Automatic" leaves it to FlightGear, which picks
// the one into the wind; a chosen runway is where a start on the ground
// lines up and what a start on final approach flies towards.
Page {
    id: page

    property var root                     // the country page, which carries the signal
    property string icao: ""
    property string name: ""
    property real lat: 0
    property real lon: 0
    property real elev: 0                 // field elevation, feet
    property var runways: []

    function pick(rwy) {
        if (page.root)
            page.root.picked(page.icao, page.name + " (" + page.icao + ")", page.lat, page.lon, rwy, page.elev)
        pageStack.pop(page.root, PageStackAction.Immediate)
        pageStack.pop()
    }

    SilicaListView {
        anchors.fill: parent
        model: [""].concat(page.runways)

        header: PageHeader {
            title: qsTr("Runway")
            description: page.name + " (" + page.icao + ")"
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Theme.itemSizeSmall
            Label {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData === "" ? qsTr("Automatic (into the wind)") : qsTr("Runway %1").arg(modelData)
                color: highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            onClicked: page.pick(modelData)
        }

        VerticalScrollDecorator {}
    }
}
