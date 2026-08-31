import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.fgview 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    property FgRuntime rt

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: "FlightGear" }

            // ---- base data missing --------------------------------

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: rt !== null && !rt.dataReady

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.highlightColor
                    text: qsTr("The base data (FGData) is missing. It contains aircraft, instruments and scenery definitions and is about 1.7 GB.")
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: qsTr("The download uses eight parallel connections and can be interrupted and resumed at any time. Wi-Fi recommended.")
                }

                Item { width: 1; height: Theme.paddingMedium }

                ProgressBar {
                    width: parent.width
                    visible: rt !== null && rt.busy && rt.progress > 0
                    minimumValue: 0
                    maximumValue: 100
                    value: rt ? rt.progress : 0
                    label: rt ? rt.status : ""
                    valueText: rt ? (rt.progress + " %  " + rt.speed) : ""
                }

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Medium
                    running: rt !== null && rt.busy && rt.progress === 0
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    visible: rt !== null && rt.busy
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: rt ? rt.status : ""
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    visible: rt !== null && !rt.busy && rt.status.length > 0
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: rt ? rt.status : ""
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (rt && rt.busy) ? qsTr("Cancel") : qsTr("Download base data")
                    onClicked: {
                        if (!rt) return
                        if (rt.busy) rt.cancelDownload()
                        else rt.downloadData()
                    }
                }
            }

            // ---- ready to fly -------------------------------------

            Column {
                width: parent.width
                spacing: Theme.paddingMedium
                visible: rt !== null && rt.dataReady

                Label {
                    x: Theme.horizontalPageMargin
                    color: Theme.highlightColor
                    text: qsTr("Base data installed")
                }

                ComboBox {
                    id: aircraftBox
                    label: qsTr("Aircraft")
                    currentIndex: 0
                    menu: ContextMenu {
                        MenuItem { text: "Cessna 172P" }
                        MenuItem { text: "Piper J3 Cub" }
                        MenuItem { text: "Cessna 172P (2D panel)" }
                    }
                    property var ids: ["c172p", "j3cub", "c172p"]
                }

                ComboBox {
                    id: backendBox
                    label: qsTr("Graphics backend")
                    currentIndex: 2
                    menu: ContextMenu {
                        MenuItem { text: qsTr("Zink (complete)") }
                        MenuItem { text: qsTr("GLES2 (native)") }
                        MenuItem { text: qsTr("GLES3 (native)") }
                    }
                    property var ids: ["zink", "gles2", "gles3"]
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    visible: backendBox.currentIndex > 0
                    text: qsTr("The native backends run without Mesa, but "
                             + "have no menus, HUD, glass cockpit displays "
                             + "or approach lights.")
                }

                TextSwitch {
                    id: airborneSwitch
                    text: qsTr("Start in the air")
                    description: qsTr("Starts at 3000 ft with the engine "
                                    + "running. On the ground the engine has "
                                    + "to be started by hand.")
                    checked: false
                }

                ComboBox {
                    id: airportBox
                    label: qsTr("Departure airport")
                    currentIndex: 0
                    menu: ContextMenu {
                        MenuItem { text: "Vienna (LOWW)" }
                        MenuItem { text: "Wiener Neustadt East (LOAN)" }
                        MenuItem { text: "Innsbruck (LOWI)" }
                        MenuItem { text: "Salzburg (LOWS)" }
                        MenuItem { text: "San Francisco (KSFO)" }
                    }
                    property var ids: ["LOWW", "LOAN", "LOWI", "LOWS", "KSFO"]
                }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: (rt && rt.simRunning) ? rt.status
                          : qsTr("The first start takes a minute or two while scenery and flight model are loaded.")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (rt && rt.simRunning) ? qsTr("Stop simulator") : qsTr("Start")
                    onClicked: {
                        if (!rt) return
                        if (rt.simRunning) {
                            rt.stopSim()
                        } else {
                            rt.startSim(aircraftBox.ids[aircraftBox.currentIndex],
                                        airportBox.ids[airportBox.currentIndex],
                                        backendBox.ids[backendBox.currentIndex],
                                        airborneSwitch.checked)
                        }
                    }
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Simulator log")
                    onClicked: pageStack.push(Qt.resolvedUrl("LogPage.qml"),
                                              { rt: rt })
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Open cockpit")
                    enabled: rt !== null && rt.simRunning
                    onClicked: pageStack.push(Qt.resolvedUrl("FlightPage.qml"))
                }
            }
        }
    }
}
