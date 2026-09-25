import QtQuick
import Cadenza

/// Centred glyph, title and message, for an empty library, a search without
/// matches, or a library that could not be opened.
Column {
    id: root

    property string title: ""
    property string message: ""
    /// Which built-in drawing to show: note, search, folder or download.
    property string glyph: "note"

    readonly property int textWidth: 380

    width: textWidth
    height: implicitHeight
    spacing: Theme.space4

    IconButton {
        anchors.horizontalCenter: parent.horizontalCenter
        size: 64
        iconSize: 56
        glyph: root.glyph
        interactive: false
        color: Theme.textFaint
    }

    Text {
        width: root.textWidth
        text: root.title
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTitle
        color: Theme.textDim
    }

    Text {
        width: root.textWidth
        text: root.message
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        color: Theme.textFaint
    }
}
