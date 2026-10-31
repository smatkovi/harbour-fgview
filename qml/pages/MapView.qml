import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

// Where the aircraft is and what is around it: airports, small airports and
// airfields, north up, the aircraft in the middle.
//
// Drawn rather than a tile map.  Tiles need a connection in flight and a
// server that accepts the traffic; the airports are already on the phone
// (make-airports.py writes them a second time by position, into
// airports/grid/, one file per 5 by 5 degree cell), and they are what this
// is for.  A flat projection around the aircraft: out to a few hundred
// nautical miles the distortion is far below what a finger can point at.
//
// Tap a symbol for its name, distance and bearing, and to have the autopilot
// turn towards it.
Item {
    id: map

    property var ctl                        // ControlSender

    readonly property string gridDir: "/usr/share/harbour-fgview/airports/grid/"
    readonly property var ranges: [2, 5, 10, 20, 50, 100, 200]
    // centre to the nearer edge, nautical miles
    readonly property real rangeNm: ranges[Math.max(0, Math.min(ranges.length - 1, mapCfg.rangeIndex))]

    // cell key "45_15" -> list of entries, or "loading"; see make-airports.py
    // for the entry: [icao, name, lat, lon, kind, runway m, runway heading]
    property var cells: ({})
    property var selected: null             // an entry
    property var hits: []                   // [x, y, entry] of what was drawn

    readonly property color largeColor: Theme.highlightColor
    readonly property color smallColor: Theme.primaryColor
    readonly property color fieldColor: "#8fd16a"
    readonly property color aircraftColor: "#ffcc00"

    ConfigurationGroup {
        id: mapCfg
        path: "/apps/harbour-fgview/map"
        property int rangeIndex: 3
    }

    // any change of position, heading or zoom draws again
    property var fix: ctl && ctl.posValid ? [ctl.posLat, ctl.posLon, ctl.posHeading] : null
    onFixChanged: canvas.requestPaint()
    onRangeNmChanged: canvas.requestPaint()
    onSelectedChanged: canvas.requestPaint()

    function loadCell(key) {
        cells[key] = "loading"
        var xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            var list = []
            try {
                // open sea has no file: an empty answer, which is an empty cell
                if (xhr.responseText !== "") list = JSON.parse(xhr.responseText)
            } catch (e) {
                list = []
            }
            cells[key] = list
            canvas.requestPaint()
        }
        xhr.open("GET", "file://" + gridDir + key + ".json")
        xhr.send()
    }

    // the cells the visible area touches, loading the ones not yet there
    function cellsAround(lat0, lon0, spanLat, spanLon) {
        var keys = []
        for (var la = Math.floor((lat0 - spanLat) / 5) * 5; la <= lat0 + spanLat; la += 5) {
            if (la < -90 || la >= 90) continue
            for (var lo = Math.floor((lon0 - spanLon) / 5) * 5; lo <= lon0 + spanLon; lo += 5) {
                var l = lo
                while (l < -180) l += 360
                while (l >= 180) l -= 360
                var key = la + "_" + l
                if (keys.indexOf(key) >= 0) continue
                keys.push(key)
                if (cells[key] === undefined) loadCell(key)
            }
        }
        return keys
    }

    // great circle distance in nautical miles and true bearing
    function distanceBearing(lat1, lon1, lat2, lon2) {
        var r = Math.PI / 180
        var p1 = lat1 * r, p2 = lat2 * r, dl = (lon2 - lon1) * r
        var a = Math.pow(Math.sin((p2 - p1) / 2), 2)
                + Math.cos(p1) * Math.cos(p2) * Math.pow(Math.sin(dl / 2), 2)
        var nm = 2 * 3440.065 * Math.asin(Math.min(1, Math.sqrt(a)))
        var y = Math.sin(dl) * Math.cos(p2)
        var x = Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dl)
        var brg = (Math.atan2(y, x) / r + 360) % 360
        return { nm: nm, bearing: brg }
    }

    // what the heading indicator and the autopilot count in
    function magnetic(trueDeg) {
        var m = Math.round(trueDeg - (ctl ? ctl.posMagVar : 0)) % 360
        return m < 0 ? m + 360 : m
    }

    function kindName(kind) {
        return [qsTr("Airport"), qsTr("Small airport"), qsTr("Airfield")][kind] || ""
    }

    Rectangle {
        anchors.fill: parent
        color: "#0c1418"
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width, h = height, cx = w / 2, cy = h / 2
            var found = []
            map.hits = []
            if (!map.ctl || !map.ctl.posValid) return

            var scale = Math.min(w, h) / 2 / map.rangeNm      // pixels per nm
            var u = Theme.pixelRatio                            // symbol sizes
            var lat0 = map.ctl.posLat, lon0 = map.ctl.posLon
            var k = Math.max(0.05, Math.cos(lat0 * Math.PI / 180))
            function project(lat, lon) {
                var dl = lon - lon0
                if (dl > 180) dl -= 360
                if (dl < -180) dl += 360
                return [cx + dl * 60 * k * scale, cy - (lat - lat0) * 60 * scale]
            }

            // range rings: the full range and half of it
            ctx.lineWidth = u
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.18)
            ctx.fillStyle = Qt.rgba(1, 1, 1, 0.45)
            ctx.font = Math.round(Theme.fontSizeTiny) + "px sans-serif"
            var rings = [map.rangeNm / 2, map.rangeNm]
            for (var ri = 0; ri < rings.length; ++ri) {
                ctx.beginPath()
                ctx.arc(cx, cy, rings[ri] * scale, 0, 2 * Math.PI, false)
                ctx.stroke()
                ctx.fillText(rings[ri] + " nm", cx + 4, cy - rings[ri] * scale + Theme.fontSizeTiny)
            }

            // Fewer kinds the further out, or the picture is only dots:
            // airfields up to 50 nm, small airports up to 100.  Names only
            // where there is room for them.
            var showKind = [true, map.rangeNm <= 100, map.rangeNm <= 50]
            var labelKind = map.rangeNm <= 10 ? 2 : map.rangeNm <= 20 ? 1 : map.rangeNm <= 100 ? 0 : -1
            var colors = [String(map.largeColor), String(map.smallColor), String(map.fieldColor)]

            var spanLat = map.rangeNm * Math.max(w, h) / Math.min(w, h) / 60
            var keys = map.cellsAround(lat0, lon0, spanLat, spanLat / k)
            // airfields first, large airports last, so those stay on top
            for (var kind = 2; kind >= 0; --kind) {
                if (!showKind[kind]) continue
                for (var ci = 0; ci < keys.length; ++ci) {
                    var list = map.cells[keys[ci]]
                    if (!list || list === "loading") continue
                    for (var i = 0; i < list.length; ++i) {
                        var e = list[i]
                        if (e[4] !== kind) continue
                        var p = project(e[2], e[3])
                        if (p[0] < -30 || p[0] > w + 30 || p[1] < -30 || p[1] > h + 30) continue

                        // the longest runway to scale, a few pixels at least
                        var half = Math.max((kind === 0 ? 9 : 6) * u, e[5] / 1852 * scale / 2)
                        var a = e[6] * Math.PI / 180
                        var dx = Math.sin(a) * half, dy = -Math.cos(a) * half
                        ctx.strokeStyle = colors[kind]
                        ctx.lineWidth = (kind === 0 ? 4 : 2.5) * u
                        ctx.beginPath()
                        ctx.moveTo(p[0] - dx, p[1] - dy)
                        ctx.lineTo(p[0] + dx, p[1] + dy)
                        ctx.stroke()
                        ctx.lineWidth = (kind === 0 ? 2 : 1.5) * u
                        ctx.beginPath()
                        ctx.arc(p[0], p[1], [10, 8, 7][kind] * u, 0, 2 * Math.PI, false)
                        ctx.stroke()

                        if (kind <= labelKind) {
                            ctx.fillStyle = colors[kind]
                            ctx.fillText(e[0], p[0] + 12 * u, p[1] + 5 * u)
                        }
                        found.push([p[0], p[1], e])
                    }
                }
            }

            // the chosen one: ringed, and a line to it
            if (map.selected) {
                var s = project(map.selected[2], map.selected[3])
                ctx.strokeStyle = String(map.aircraftColor)
                ctx.lineWidth = 2 * u
                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.lineTo(s[0], s[1])
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(s[0], s[1], 16 * u, 0, 2 * Math.PI, false)
                ctx.stroke()
            }

            // the aircraft, pointing where it flies, with a line ahead to
            // the half-range ring
            var hdg = map.ctl.posHeading * Math.PI / 180
            ctx.strokeStyle = Qt.rgba(1, 0.8, 0, 0.5)
            ctx.lineWidth = 1.5 * u
            ctx.beginPath()
            ctx.moveTo(cx, cy)
            ctx.lineTo(cx + Math.sin(hdg) * map.rangeNm / 2 * scale,
                       cy - Math.cos(hdg) * map.rangeNm / 2 * scale)
            ctx.stroke()
            ctx.save()
            ctx.translate(cx, cy)
            ctx.rotate(hdg)
            ctx.fillStyle = String(map.aircraftColor)
            ctx.strokeStyle = "black"
            ctx.lineWidth = u
            ctx.beginPath()
            ctx.moveTo(0, -16 * u)
            ctx.lineTo(10 * u, 11 * u)
            ctx.lineTo(0, 6 * u)
            ctx.lineTo(-10 * u, 11 * u)
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
            ctx.restore()
            map.hits = found
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            var best = null, bestD = Theme.itemSizeSmall * 0.6
            for (var i = 0; i < map.hits.length; ++i) {
                var d = Math.sqrt(Math.pow(map.hits[i][0] - mouse.x, 2)
                                  + Math.pow(map.hits[i][1] - mouse.y, 2))
                if (d < bestD) { bestD = d; best = map.hits[i][2] }
            }
            map.selected = best
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !map.fix
        width: parent.width - 2 * Theme.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.secondaryHighlightColor
        text: qsTr("Waiting for the position from the simulator…")
    }

    // ---- top: scale, north, zoom -----------------------------------------
    Rectangle {
        anchors { fill: status; margins: -Theme.paddingSmall }
        radius: Theme.paddingSmall
        color: Qt.rgba(0, 0, 0, 0.6)
    }
    Column {
        id: status
        anchors { left: parent.left; top: parent.top; margins: Theme.paddingMedium }
        Label {
            text: qsTr("North up · %1 nm").arg(map.rangeNm)
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
        Label {
            visible: map.fix !== null
            text: ctl ? qsTr("%1 ft · %2 kt · %3°").arg(Math.round(ctl.posAltitude))
                                                  .arg(Math.round(ctl.posGroundSpeed))
                                                  .arg(map.magnetic(ctl.posHeading))
                      : ""
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
    }

    Column {
        anchors { right: parent.right; top: parent.top; margins: Theme.paddingMedium }
        spacing: Theme.paddingSmall
        FlatButton {
            width: Theme.itemSizeSmall
            text: "+"
            enabled: mapCfg.rangeIndex > 0
            onClicked: mapCfg.rangeIndex = Math.max(0, mapCfg.rangeIndex - 1)
        }
        FlatButton {
            width: Theme.itemSizeSmall
            text: "−"
            enabled: mapCfg.rangeIndex < map.ranges.length - 1
            onClicked: mapCfg.rangeIndex = Math.min(map.ranges.length - 1, mapCfg.rangeIndex + 1)
        }
    }

    // ---- bottom: the legend, or what was tapped ---------------------------
    Rectangle {
        visible: legend.visible
        anchors { fill: legend; margins: -Theme.paddingSmall }
        radius: Theme.paddingSmall
        color: Qt.rgba(0, 0, 0, 0.6)
    }
    Row {
        id: legend
        visible: !map.selected
        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.paddingMedium }
        spacing: Theme.paddingMedium
        Label { text: "◯ " + qsTr("Airport"); color: map.largeColor; font.pixelSize: Theme.fontSizeTiny }
        Label { text: "◯ " + qsTr("Small airport"); color: map.smallColor; font.pixelSize: Theme.fontSizeTiny }
        Label { text: "◯ " + qsTr("Airfield"); color: map.fieldColor; font.pixelSize: Theme.fontSizeTiny }
    }

    Rectangle {
        id: info
        visible: map.selected !== null
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.paddingSmall }
        height: infoColumn.height + 2 * Theme.paddingMedium
        radius: Theme.paddingSmall
        color: Qt.rgba(0, 0, 0, 0.75)

        // recomputed with every position update
        property var db: map.selected && map.fix
                         ? map.distanceBearing(map.fix[0], map.fix[1], map.selected[2], map.selected[3])
                         : null

        Column {
            id: infoColumn
            anchors {
                left: parent.left; right: buttons.left; top: parent.top
                margins: Theme.paddingMedium
            }
            Label {
                width: parent.width
                text: map.selected ? map.selected[1] + " (" + map.selected[0] + ")" : ""
                truncationMode: TruncationMode.Fade
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: map.selected && info.db
                      ? qsTr("%1 · %2 m runway · %3 nm, course %4°")
                        .arg(map.kindName(map.selected[4])).arg(map.selected[5])
                        .arg(info.db.nm < 10 ? info.db.nm.toFixed(1) : Math.round(info.db.nm))
                        .arg(map.magnetic(info.db.bearing))
                      : ""
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }
        }
        Row {
            id: buttons
            anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: Theme.paddingMedium }
            spacing: Theme.paddingSmall
            FlatButton {
                width: Theme.itemSizeExtraLarge
                text: qsTr("Fly there")
                enabled: info.db !== null
                onClicked: ctl.apFlyHeading(map.magnetic(info.db.bearing))
            }
            FlatButton {
                width: Theme.itemSizeSmall
                text: "✕"
                onClicked: map.selected = null
            }
        }
    }
}
