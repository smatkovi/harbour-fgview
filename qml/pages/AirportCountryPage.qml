import QtQuick 2.6
import Sailfish.Silica 1.0

// Step one of picking a departure airport: the country.
//
// The lists are generated once from FGData's apt.dat.gz (see
// make-airports.py) and shipped with the app, one small file per country
// plus this index.  27000 airports live in there, which is why the picker
// narrows by country first and only then loads a list.
Page {
    id: page

    // Emitted once an airport has been chosen, anywhere down the stack.
    // The pages below reach back up to here, so the caller only has to
    // connect to this one.
    signal picked(string icao, string label, real lat, real lon)

    readonly property string dataDir: "/usr/share/harbour-fgview/airports/"

    property var countries: []
    property string filter: ""

    function load() {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (xhr.status !== 200 && xhr.status !== 0) {
                page.countries = []
                return
            }
            try {
                page.countries = JSON.parse(xhr.responseText)
            } catch (e) {
                page.countries = []
            }
        }
        xhr.open("GET", "file://" + dataDir + "countries.json")
        xhr.send()
    }

    Component.onCompleted: load()

    // Filtering in a plain function rather than a FilterProxyModel: the
    // index is 213 entries, so rebuilding the array on each keystroke is
    // cheaper than keeping a model in sync.
    function shown() {
        if (filter === "") return countries
        var f = filter.toLowerCase()
        var out = []
        for (var i = 0; i < countries.length; ++i)
            if (countries[i].name.toLowerCase().indexOf(f) >= 0)
                out.push(countries[i])
        return out
    }

    SilicaListView {
        anchors.fill: parent
        model: page.shown()

        header: Column {
            width: page.width
            PageHeader { title: qsTr("Country") }
            SearchField {
                width: parent.width
                placeholderText: qsTr("Search country")
                onTextChanged: page.filter = text
                EnterKey.onClicked: focus = false
            }
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Theme.itemSizeSmall

            Label {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData.name
                color: highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                anchors {
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData.large + " / " + modelData.small
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            onClicked: {
                var p = pageStack.push(Qt.resolvedUrl("AirportSizePage.qml"), {
                    root: page,
                    countryName: modelData.name,
                    file: page.dataDir + modelData.file,
                    largeCount: modelData.large,
                    smallCount: modelData.small
                })
            }
        }

        ViewPlaceholder {
            enabled: page.countries.length === 0
            text: qsTr("No airport data")
            hintText: qsTr("The lists are installed with the app under %1").arg(page.dataDir)
        }

        VerticalScrollDecorator {}
    }
}
