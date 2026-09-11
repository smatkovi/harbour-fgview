import QtQuick 2.6
import Sailfish.Silica 1.0

// What is installed, and a way to the hangar for what is not.
//
// The chooser this replaces held three fixed entries, one of which pointed
// at "j3cub" - an aircraft that is neither in FGData nor in the catalogue,
// where the Cub is called J3Cub.  Picking it started nothing at all.
Page {
    id: page

    property var rt                        // FgRuntime
    signal picked(string id, string label)

    SilicaListView {
        anchors.fill: parent
        model: rt ? rt.aircraft : []

        PullDownMenu {
            MenuItem {
                text: qsTr("Get more aircraft")
                onClicked: {
                    var p = pageStack.push(Qt.resolvedUrl("HangarPage.qml"),
                                           { rt: page.rt })
                }
            }
            MenuItem {
                text: qsTr("Rescan")
                onClicked: if (rt) rt.refreshAircraft()
            }
        }

        header: Column {
            width: page.width
            PageHeader {
                title: qsTr("Aircraft")
                description: rt ? qsTr("%1 installed").arg(rt.aircraft.length) : ""
            }
            // A download started in the hangar keeps running after leaving
            // that page, so say so here too rather than letting it look as
            // if nothing were happening.
            Item {
                width: parent.width
                height: visible ? busyRow.height + 2 * Theme.paddingMedium : 0
                visible: rt !== undefined && rt !== null && rt.catalogBusy
                Row {
                    id: busyRow
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.paddingMedium
                    BusyIndicator {
                        running: parent.parent.visible
                        size: BusyIndicatorSize.Small
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width - Theme.itemSizeSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: rt ? rt.hangarStatus : ""
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeSmall
                        truncationMode: TruncationMode.Fade
                    }
                }
            }
        }

        delegate: ListItem {
            width: page.width
            contentHeight: Theme.itemSizeSmall

            Column {
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
                    text: modelData.id
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Remove")
                    onClicked: if (rt) rt.removeAircraft(modelData.dir)
                }
            }

            onClicked: {
                page.picked(modelData.id, modelData.name)
                pageStack.pop()
            }
        }

        ViewPlaceholder {
            enabled: rt && rt.aircraft.length === 0
            text: qsTr("No aircraft found")
            hintText: qsTr("Pull down to fetch some from the hangar")
        }

        VerticalScrollDecorator {}
    }
}
