import QtQuick 2.6
import Sailfish.Silica 1.0

// Step three: the airports themselves, longest runway first - in any
// country the one being looked for is usually its biggest.
Page {
    id: page

    property var root                 // the country page, which carries the signal
    property string countryName: ""
    property string file: ""
    property string which: "large"

    property var all: []
    property string filter: ""

    function load() {
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            if (xhr.status !== 200 && xhr.status !== 0) { page.all = []; return }
            try {
                var d = JSON.parse(xhr.responseText)
                page.all = d[page.which] || []
            } catch (e) {
                page.all = []
            }
        }
        xhr.open("GET", "file://" + page.file)
        xhr.send()
    }

    Component.onCompleted: load()

    // Entries are [icao, name, longest runway in metres].
    function shown() {
        if (filter === "") return all
        var f = filter.toLowerCase()
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (all[i][0].toLowerCase().indexOf(f) >= 0
                || all[i][1].toLowerCase().indexOf(f) >= 0)
                out.push(all[i])
        return out
    }

    SilicaListView {
        anchors.fill: parent
        model: page.shown()

        header: Column {
            width: page.width
            PageHeader {
                title: page.which === "large" ? qsTr("Large airports")
                                              : qsTr("Small airports")
                description: page.countryName
            }
            SearchField {
                width: parent.width
                placeholderText: qsTr("Search name or code")
                onTextChanged: page.filter = text
                EnterKey.onClicked: focus = false
            }
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Theme.itemSizeSmall

            Column {
                anchors {
                    left: parent.left; leftMargin: Theme.horizontalPageMargin
                    right: lengthLabel.left; rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData[1]
                    truncationMode: TruncationMode.Fade
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    text: modelData[0]
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }
            Label {
                id: lengthLabel
                anchors {
                    right: parent.right; rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData[2] + " m"
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            onClicked: {
                // Reach back to the country page, which is what the caller
                // connected to, then unwind the whole picker in one go.
                if (page.root)
                    page.root.picked(modelData[0], modelData[1] + " (" + modelData[0] + ")",
                                     modelData.length > 4 ? modelData[3] : 0,
                                     modelData.length > 4 ? modelData[4] : 0)
                pageStack.pop(page.root, PageStackAction.Immediate)
                pageStack.pop()
            }
        }

        ViewPlaceholder {
            enabled: page.all.length === 0
            text: qsTr("Nothing here")
        }

        VerticalScrollDecorator {}
    }
}
