import QtQuick 2.6
import Sailfish.Silica 1.0

// FlightGear's tutorials for the loaded aircraft: step-by-step lessons the
// simulator checks as you go (the c172p brings fourteen, from preflight to
// engine failure).  Read from the running simulator, started through it;
// the instructions appear on the flight page, because the window
// FlightGear would show them in is PUI, which this backend lacks.
Page {
    id: page

    property var ctl                      // ControlSender of the flight page
    property var rt                       // FgRuntime, for the scenery
    property var pending: null            // lesson waiting for its scenery

    Component.onCompleted: if (ctl) ctl.refreshTutorials()

    function start(lesson) {
        // The lesson repositions the aircraft (the c172p's are at Hilo);
        // get the tiles there first, or the lesson starts over water.
        if (rt && lesson.lat !== 0 && lesson.lon !== 0 && !rt.sceneryBusy) {
            page.pending = lesson
            rt.fetchSceneryFor(lesson.lat, lesson.lon)
        } else {
            ctl.startTutorial(lesson.name)
            pageStack.pop()
        }
    }

    Connections {
        target: rt
        onSceneryReady: {
            if (!page.pending) return
            var l = page.pending
            page.pending = null
            ctl.startTutorial(l.name)
            pageStack.pop()
        }
    }

    SilicaListView {
        anchors.fill: parent
        model: ctl ? ctl.tutorials : []

        PullDownMenu {
            MenuItem {
                text: qsTr("Stop lesson")
                visible: ctl && ctl.tutorialRunning
                onClicked: ctl.stopTutorial()
            }
            MenuItem {
                text: qsTr("Reload list")
                onClicked: if (ctl) ctl.refreshTutorials()
            }
        }

        header: Column {
            width: page.width
            PageHeader {
                title: qsTr("Lessons")
                description: ctl && ctl.tutorialRunning ? qsTr("Running: %1").arg(ctl.tutorialName)
                            : (ctl ? qsTr("%1 available").arg(ctl.tutorials.length) : "")
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                wrapMode: Text.WordWrap; font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                visible: page.pending !== null
                text: rt ? rt.status : ""
            }
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Math.max(Theme.itemSizeMedium, col.height + Theme.paddingMedium)
            enabled: page.pending === null
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
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: (modelData.airport ? modelData.airport + " — " : "") + modelData.description
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }
            onClicked: page.start(modelData)
        }

        ViewPlaceholder {
            enabled: !ctl || ctl.tutorials.length === 0
            text: qsTr("No lessons")
            hintText: qsTr("This aircraft has none, or the simulator is still starting — pull down to reload")
        }

        VerticalScrollDecorator {}
    }
}
