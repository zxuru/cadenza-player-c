import QtQuick
import Cadenza

/// A square icon button. Every glyph is a hand-drawn canvas path: the Basic
/// style ships no icon font, and a missing glyph would paint as an empty box.
Item {
    id: control

    /// Side of the button, and therefore of the hover fill behind the glyph.
    property int size: 32
    property int iconSize: 18
    /// Which built-in drawing to render: play, pause, previous, next, volume,
    /// mute, folder, refresh, search, download, check, lyrics, note, album,
    /// repeatone, shuffle, pencil, chevron, minimize, maximize, restore or
    /// close.
    property string glyph: "play"
    /// Draws the active fill, for toggles that are currently on.
    property bool active: false
    /// Filled with the accent colour instead of a hover tint.
    property bool accent: false
    /// Filled with the danger colour while hovered, for the control that
    /// closes the window.
    property bool danger: false
    /// Circular instead of rounded, for the transport's play button.
    property bool round: false
    /// False turns the button into a plain icon: no hover fill, no tooltip.
    property bool interactive: true
    /// True while the button's work is in flight: the glyph turns, so a button
    /// whose fill looks the same before and after says that it is still going.
    property bool spinning: false
    property color color: control.accent
                          ? Theme.accentText
                          : (control.danger && control.hovered ? Theme.dangerText : Theme.text)
    property string tooltip: ""

    signal clicked()

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    readonly property bool hovered: interactive && pointer.containsMouse

    /// Paints `glyph` into `ctx`, authored on a 24 x 24 grid so one set of
    /// coordinates works at every icon size.
    function paintGlyph(ctx, w, h)
    {
        ctx.clearRect(0, 0, w, h)
        ctx.save()
        var scale = Math.min(w, h) / 24
        ctx.translate((w - 24 * scale) / 2, (h - 24 * scale) / 2)
        ctx.scale(scale, scale)
        ctx.fillStyle = control.color
        ctx.strokeStyle = control.color
        ctx.lineWidth = 2
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        switch (control.glyph) {
        case "play":
            ctx.beginPath()
            ctx.moveTo(8.6, 5.2)
            ctx.lineTo(19.0, 12.0)
            ctx.lineTo(8.6, 18.8)
            ctx.closePath()
            ctx.fill()
            ctx.lineWidth = 1.4
            ctx.stroke()
            break
        case "pause":
            ctx.beginPath()
            ctx.roundedRect(8.0, 5.0, 3.4, 14.0, 1.6, 1.6)
            ctx.fill()
            ctx.beginPath()
            ctx.roundedRect(12.6, 5.0, 3.4, 14.0, 1.6, 1.6)
            ctx.fill()
            break
        case "previous":
            ctx.beginPath()
            ctx.roundedRect(5.6, 6.4, 2.8, 11.2, 1.3, 1.3)
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(19.4, 5.2)
            ctx.lineTo(19.4, 18.8)
            ctx.lineTo(9.0, 12.0)
            ctx.closePath()
            ctx.fill()
            ctx.lineWidth = 1.4
            ctx.stroke()
            break
        case "next":
            ctx.beginPath()
            ctx.roundedRect(15.6, 6.4, 2.8, 11.2, 1.3, 1.3)
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(4.6, 5.2)
            ctx.lineTo(4.6, 18.8)
            ctx.lineTo(15.0, 12.0)
            ctx.closePath()
            ctx.fill()
            ctx.lineWidth = 1.4
            ctx.stroke()
            break
        case "volume":
            speakerBody(ctx)
            ctx.beginPath()
            ctx.arc(11.4, 12.0, 4.4, -0.95, 0.95)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(11.4, 12.0, 7.4, -0.95, 0.95)
            ctx.stroke()
            break
        case "mute":
            speakerBody(ctx)
            ctx.beginPath()
            ctx.moveTo(14.2, 8.6)
            ctx.lineTo(20.2, 15.4)
            ctx.stroke()
            break
        case "folder":
            ctx.beginPath()
            ctx.moveTo(3.0, 7.4)
            ctx.lineTo(9.6, 7.4)
            ctx.lineTo(11.4, 10.0)
            ctx.lineTo(21.0, 10.0)
            ctx.lineTo(21.0, 19.2)
            ctx.lineTo(3.0, 19.2)
            ctx.closePath()
            ctx.fill()
            ctx.lineWidth = 1.4
            ctx.stroke()
            break
        case "refresh":
            ctx.beginPath()
            ctx.arc(12.0, 12.0, 6.6, -0.06, 5.0)
            ctx.stroke()
            arrowHead(ctx, 6.6, 5.0, 4.4, 2.6)
            break
        case "search":
            ctx.beginPath()
            ctx.arc(10.4, 10.4, 5.4, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(14.4, 14.4)
            ctx.lineTo(19.6, 19.6)
            ctx.stroke()
            break
        case "download":
            // An arrow into a tray: the one glyph in the window that stands
            // for something arriving from outside it.
            ctx.beginPath()
            ctx.moveTo(12.0, 3.8)
            ctx.lineTo(12.0, 14.2)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(7.4, 9.8)
            ctx.lineTo(12.0, 14.4)
            ctx.lineTo(16.6, 9.8)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(4.8, 19.6)
            ctx.lineTo(19.2, 19.6)
            ctx.stroke()
            break
        case "check":
            ctx.beginPath()
            ctx.moveTo(5.2, 12.8)
            ctx.lineTo(10.0, 17.4)
            ctx.lineTo(18.8, 6.6)
            ctx.stroke()
            break
        case "lyrics":
            // A page of text: the frame is stroked, the lines filled, so it
            // still reads at 16 pixels.
            ctx.beginPath()
            ctx.roundedRect(4.8, 5.2, 14.4, 13.6, 2.0, 2.0)
            ctx.lineWidth = 1.6
            ctx.stroke()
            ctx.beginPath()
            ctx.roundedRect(7.6, 8.8, 8.8, 1.8, 0.9, 0.9)
            ctx.fill()
            ctx.beginPath()
            ctx.roundedRect(7.6, 12.2, 6.2, 1.8, 0.9, 0.9)
            ctx.fill()
            break
        case "note":
            ctx.save()
            ctx.translate(10.0, 16.6)
            ctx.rotate(-0.38)
            ctx.scale(3.6, 2.6)
            ctx.beginPath()
            ctx.arc(0, 0, 1, 0, Math.PI * 2)
            ctx.restore()
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(13.4, 16.0)
            ctx.lineTo(13.4, 4.6)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(13.4, 4.6)
            ctx.quadraticCurveTo(19.6, 6.2, 14.0, 11.2)
            ctx.stroke()
            break
        case "album":
            // A record: the disc itself, and the hole at its centre. The one
            // mark for an album that is not a stack of them.
            ctx.lineWidth = 1.7
            ctx.beginPath()
            ctx.arc(12.0, 12.0, 8.2, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(12.0, 12.0, 2.1, 0, Math.PI * 2)
            ctx.fill()
            break
        case "repeatone":
            // A loop with a 1 in it: the same track again, which is what the
            // per-track mode of ReplayGain is drawn as.
            ctx.lineWidth = 1.7
            ctx.beginPath()
            ctx.arc(12.0, 12.0, 7.6, -0.5, 5.05)
            ctx.stroke()
            arrowHead(ctx, 7.6, 5.05, 4.6, 2.7)
            // The 1: the flag at its head, the stem, and the foot under it.
            ctx.lineWidth = 1.9
            ctx.beginPath()
            ctx.moveTo(10.4, 10.0)
            ctx.lineTo(12.6, 8.3)
            ctx.lineTo(12.6, 15.5)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(10.4, 15.5)
            ctx.lineTo(14.8, 15.5)
            ctx.stroke()
            break
        case "shuffle":
            // Two paths crossing: what plays next is not what the list says.
            // Each runs from the left edge to the right one, and the head at
            // the end of each points the way it was travelling.
            ctx.beginPath()
            ctx.moveTo(3.4, 17.6)
            ctx.quadraticCurveTo(9.4, 17.6, 12.0, 12.0)
            ctx.quadraticCurveTo(14.6, 6.4, 20.6, 6.4)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(3.4, 6.4)
            ctx.quadraticCurveTo(9.4, 6.4, 12.0, 12.0)
            ctx.quadraticCurveTo(14.6, 17.6, 20.6, 17.6)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(21.4, 6.4)
            ctx.lineTo(17.2, 3.2)
            ctx.lineTo(17.2, 9.6)
            ctx.closePath()
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(21.4, 17.6)
            ctx.lineTo(17.2, 14.4)
            ctx.lineTo(17.2, 20.8)
            ctx.closePath()
            ctx.fill()
            break
        case "pencil":
            // A pencil on the diagonal, its point at the lower left: the one
            // glyph that says a name can be written again.
            ctx.save()
            ctx.translate(12.0, 11.6)
            ctx.rotate(Math.PI / 4)
            ctx.beginPath()
            ctx.roundedRect(-2.6, -8.6, 5.2, 13.2, 1.4, 1.4)
            ctx.fill()
            ctx.beginPath()
            ctx.moveTo(-2.6, 4.6)
            ctx.lineTo(2.6, 4.6)
            ctx.lineTo(0.0, 9.6)
            ctx.closePath()
            ctx.fill()
            ctx.lineWidth = 1.2
            ctx.beginPath()
            ctx.moveTo(-2.6, 4.6)
            ctx.lineTo(2.6, 4.6)
            ctx.stroke()
            ctx.restore()
            break
        case "chevron":
            // A caret pointing down, narrower than the arrow heads above: the
            // mark that says a control opens something under it.
            ctx.lineWidth = 1.8
            ctx.beginPath()
            ctx.moveTo(7.4, 9.8)
            ctx.lineTo(12.0, 14.4)
            ctx.lineTo(16.6, 9.8)
            ctx.stroke()
            break
        case "minimize":
            ctx.beginPath()
            ctx.moveTo(5.6, 12.5)
            ctx.lineTo(18.4, 12.5)
            ctx.stroke()
            break
        case "maximize":
            ctx.beginPath()
            ctx.roundedRect(5.9, 5.9, 12.2, 12.2, 2.0, 2.0)
            ctx.stroke()
            break
        case "restore":
            // The window behind, drawn only as far as the front one covers it.
            ctx.beginPath()
            ctx.moveTo(9.6, 6.5)
            ctx.lineTo(15.3, 6.5)
            ctx.quadraticCurveTo(17.1, 6.5, 17.1, 8.3)
            ctx.lineTo(17.1, 12.9)
            ctx.stroke()
            ctx.beginPath()
            ctx.roundedRect(6.7, 9.6, 10.4, 10.4, 1.8, 1.8)
            ctx.stroke()
            break
        case "close":
            ctx.beginPath()
            ctx.moveTo(6.9, 6.9)
            ctx.lineTo(17.1, 17.1)
            ctx.moveTo(17.1, 6.9)
            ctx.lineTo(6.9, 17.1)
            ctx.stroke()
            break
        }
        ctx.restore()
    }

    /// The head every cyclic glyph ends on: a triangle sitting at `angle` on
    /// the circle of `radius` the glyph is drawn on -- the whole grid is
    /// centred on 12, 12 -- pointing the way the arc was travelling, with
    /// `length` of arrow and `half` of width either side of the arc.
    function arrowHead(ctx, radius, angle, length, half)
    {
        const endX = 12.0 + radius * Math.cos(angle)
        const endY = 12.0 + radius * Math.sin(angle)
        const tangentX = -Math.sin(angle)
        const tangentY = Math.cos(angle)
        const normalX = Math.cos(angle)
        const normalY = Math.sin(angle)
        ctx.beginPath()
        ctx.moveTo(endX + tangentX * length, endY + tangentY * length)
        ctx.lineTo(endX + normalX * half, endY + normalY * half)
        ctx.lineTo(endX - normalX * half, endY - normalY * half)
        ctx.closePath()
        ctx.fill()
    }

    /// The cone shared by the volume and mute glyphs.
    function speakerBody(ctx)
    {
        ctx.beginPath()
        ctx.moveTo(3.4, 9.6)
        ctx.lineTo(7.0, 9.6)
        ctx.lineTo(11.4, 5.6)
        ctx.lineTo(11.4, 18.4)
        ctx.lineTo(7.0, 14.4)
        ctx.lineTo(3.4, 14.4)
        ctx.closePath()
        ctx.fill()
    }

    Rectangle {
        anchors.centerIn: parent
        width: control.size
        height: control.size
        radius: control.round ? width / 2 : Theme.radiusSmall
        color: control.danger && control.hovered
               ? Theme.danger
               : (control.accent
                  ? (control.hovered ? Qt.lighter(Theme.accent, 1.12) : Theme.accent)
                  : (control.active ? Theme.surfaceActive
                                    : (control.hovered ? Theme.surfaceHover : "transparent")))
        Behavior on color { ColorAnimation { duration: Theme.duration } }
    }

    Canvas {
        anchors.centerIn: parent
        width: control.iconSize
        height: control.iconSize

        // The canvas mirrors what it draws, which is the documented way to
        // repaint a Canvas when its input changes.
        property string paintedGlyph: control.glyph
        property color paintedColor: control.color
        onPaintedGlyphChanged: requestPaint()
        onPaintedColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: control.paintGlyph(getContext("2d"), width, height)

        // What turns while the button is busy. The angle lives in a property of
        // its own rather than in `rotation` because a stopped animation keeps
        // the angle it died at, which would leave the glyph leaning; drawn only
        // while the button is busy, it is upright the moment it is not. Only
        // the glyph turns -- the fill behind it and the tooltip above it stay
        // where they are.
        property real spinAngle: 0
        rotation: control.spinning ? spinAngle : 0

        NumberAnimation on spinAngle {
            running: control.spinning
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }

    Rectangle {
        // Left-aligned and above, so a tooltip near the window's edge still has
        // room to be read.
        anchors { left: parent.left; bottom: parent.top; bottomMargin: Theme.space1 }
        width: tooltipLabel.implicitWidth + Theme.space2 * 2
        height: 22
        radius: Theme.radiusSmall
        // Solid rather than glass: a tooltip has to be read over whatever it
        // happens to be covering.
        color: Theme.alpha(Theme.bg, 0.94)
        border.width: 1
        border.color: Theme.border
        visible: control.hovered && control.tooltip.length > 0
        z: 100

        Text {
            id: tooltipLabel
            anchors.centerIn: parent
            text: control.tooltip
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            color: Theme.text
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        enabled: control.interactive
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
