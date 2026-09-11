import QtQuick 2.6
import Sailfish.Silica 1.0

// FlightGear's AI scenarios from FGData/AI: carriers to land on, tankers,
// wingmen, ships, balloons.  A carrier scenario starts the aircraft on the
// deck; the others leave it where the airport is, and most of them sit
// off San Francisco - the position is shown so nobody looks for a tanker
// over Vienna.
Page {
    id: page

    property var rt                       // FgRuntime
    signal picked(string id, string label, string carrier, real lat, real lon)

    function where(s) {
        if (s.carrier !== "") return qsTr("Starts on the carrier %1").arg(s.carrier)
        if (s.lat !== 0 || s.lon !== 0)
            return qsTr("Near %1, %2").arg(s.lat.toFixed(1)).arg(s.lon.toFixed(1))
        return qsTr("Follows the aircraft")
    }

    SilicaListView {
        anchors.fill: parent
        model: rt ? [{ id: "", name: qsTr("None"), description: qsTr("No AI objects; the fastest start"), carrier: "", lat: 0, lon: 0 }]
                    .concat(rt.scenarios) : []

        header: PageHeader { title: qsTr("Scenario") }

        delegate: ListItem {
            width: page.width
            contentHeight: Math.max(Theme.itemSizeMedium, col.height + Theme.paddingMedium)
            Column {
                id: col
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData.name
                    truncationMode: TruncationMode.Fade
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: (modelData.id === "" ? "" : page.where(modelData) + (modelData.description ? " — " : ""))
                          + modelData.description
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }
            onClicked: {
                page.picked(modelData.id, modelData.id === "" ? "" : modelData.name,
                            modelData.carrier, modelData.lat, modelData.lon)
                pageStack.pop()
            }
        }

        VerticalScrollDecorator {}
    }
}
