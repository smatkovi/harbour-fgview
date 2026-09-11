import QtQuick 2.6
import Sailfish.Silica 1.0

// The FlightGear hangar: 648 aircraft from the official 2020 catalogue at
// mirrors.ibiblio.org.  Tapping one downloads its zip and unpacks it next
// to FGData - not into it, because FGData is replaced wholesale whenever
// the base data is re-fetched.
//
// The catalogue is fetched on opening rather than shipped: it changes
// without the app changing, and it is 1.7 MB.
Page {
    id: page

    property var rt                     // FgRuntime
    property string filter: ""

    Component.onCompleted: {
        if (rt && rt.catalog.length === 0) rt.fetchCatalog()
    }

    // Entries carry id, name, dir, url and the catalogue's own 0-5 ratings
    // for flight model and 3D model.
    function shown() {
        if (!rt) return []
        var all = rt.catalog
        if (filter === "") return all
        var f = filter.toLowerCase()
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (all[i].name.toLowerCase().indexOf(f) >= 0
                || all[i].id.toLowerCase().indexOf(f) >= 0)
                out.push(all[i])
        return out
    }

    function isInstalled(id) {
        if (!rt) return false
        for (var i = 0; i < rt.aircraft.length; ++i)
            if (rt.aircraft[i].id === id) return true
        return false
    }

    SilicaListView {
        anchors.fill: parent
        model: page.shown()

        header: Column {
            width: page.width
            PageHeader {
                title: qsTr("Hangar")
                description: rt ? rt.hangarStatus : ""
            }
            SearchField {
                width: parent.width
                placeholderText: qsTr("Search aircraft")
                onTextChanged: page.filter = text
                EnterKey.onClicked: focus = false
            }
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Theme.itemSizeSmall
            enabled: rt && !rt.catalogBusy

            Column {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    right: stars.left; rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData.name
                    truncationMode: TruncationMode.Fade
                    color: page.isInstalled(modelData.id) ? Theme.secondaryHighlightColor
                         : (highlighted ? Theme.highlightColor : Theme.primaryColor)
                }
                Label {
                    text: page.isInstalled(modelData.id)
                          ? modelData.id + " — " + qsTr("installed")
                          : modelData.id
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }

            // The catalogue's own rating, so a finished aircraft is not
            // buried among abandoned sketches.
            Label {
                id: stars
                anchors {
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: "★" + modelData.fdm + " ✈" + modelData.model
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            onClicked: {
                if (!rt || rt.catalogBusy) return
                rt.installAircraft(modelData.id, modelData.dir, modelData.url)
            }
        }

        ViewPlaceholder {
            enabled: rt && rt.catalog.length === 0 && !rt.catalogBusy
            text: qsTr("No list")
            hintText: qsTr("The hangar could not be reached")
        }

        VerticalScrollDecorator {}
    }

    // One download at a time, and the page says which, how far along and how
    // fast.  A 66 MB aircraft on a slow line is a long wait, and a spinner
    // with no number looks the same as a hang.
    Rectangle {
        id: busyOverlay
        anchors.fill: parent
        color: Theme.rgba(Theme.highlightDimmerColor, 0.85)
        visible: rt !== undefined && rt !== null && rt.catalogBusy

        Column {
            anchors.centerIn: parent
            spacing: Theme.paddingLarge
            width: parent.width - 4 * Theme.horizontalPageMargin

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: busyOverlay.visible
                size: BusyIndicatorSize.Large
            }

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: rt ? rt.hangarStatus : ""
                color: Theme.highlightColor
            }

            // Only while there is a real percentage: fetching the catalogue
            // and unpacking have none, and a bar stuck at zero would be a
            // worse answer than no bar.
            ProgressBar {
                width: parent.width
                visible: rt && rt.hangarProgress >= 0
                minimumValue: 0
                maximumValue: 100
                value: rt ? rt.hangarProgress : 0
            }
        }

        MouseArea { anchors.fill: parent }     // swallow taps while busy
    }

    // What happened, kept on screen after the overlay has gone.
    //
    // Without this the whole thing is invisible when it goes well: a 22 MB
    // aircraft on a good line is done in five seconds, so the bar is seen
    // once at nought and then the overlay disappears - which reads as a
    // failure even though the aircraft is installed.
    property string resultText: ""

    Connections {
        target: rt
        onCatalogChanged: {
            if (!rt) return
            if (rt.catalogBusy) {
                page.resultText = ""
                resultTimer.stop()
            } else if (rt.hangarStatus !== "" && page.wasBusy) {
                page.resultText = rt.hangarStatus
                resultTimer.restart()
            }
            page.wasBusy = rt.catalogBusy
        }
    }
    property bool wasBusy: false

    Timer { id: resultTimer; interval: 6000; onTriggered: page.resultText = "" }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: resultLabel.height + 2 * Theme.paddingLarge
        color: Theme.rgba(Theme.highlightBackgroundColor, 0.95)
        visible: page.resultText !== ""

        Label {
            id: resultLabel
            anchors {
                left: parent.left; right: parent.right
                leftMargin: Theme.horizontalPageMargin
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            text: page.resultText
            wrapMode: Text.WordWrap
            color: Theme.primaryColor
        }

        MouseArea { anchors.fill: parent; onClicked: page.resultText = "" }
    }
}
