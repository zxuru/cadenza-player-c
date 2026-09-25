import QtQuick
import Cadenza

/// A pill that is a state rather than an action: the accent fill and border
/// say the thing it names is on, a plain border says it is off, and the whole
/// pill is what turns it over. The same shape the transport uses for
/// EXCLUSIVE, so a switch is always a pill whether it sits in a view or in a
/// page of settings.
Rectangle {
    id: pill

    /// The words on the pill.
    property string label: ""
    /// A glyph in front of them, out of IconButton's set. An empty string
    /// draws none.
    property string glyph: ""
    property bool on: false

    signal toggled()

    readonly property bool hovered: pointer.containsMouse

    width: (pill.glyph.length > 0 ? pillGlyph.width + Theme.space1 : 0)
           + pillLabel.implicitWidth + Theme.space2 * 2
    height: 28
    radius: Theme.radiusPill
    color: pill.on ? Theme.accentSoft : (pill.hovered ? Theme.surfaceHover : "transparent")
    border.width: 1
    border.color: pill.on ? Theme.accent : Theme.border
    Behavior on color { ColorAnimation { duration: Theme.duration } }

    IconButton {
        id: pillGlyph
        anchors {
            left: parent.left
            leftMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        size: 14
        iconSize: 14
        glyph: pill.glyph
        interactive: false
        color: pill.on ? Theme.accent : Theme.textDim
        visible: pill.glyph.length > 0
    }

    Text {
        id: pillLabel
        anchors {
            left: pill.glyph.length > 0 ? pillGlyph.right : parent.left
            leftMargin: pill.glyph.length > 0 ? Theme.space1 : Theme.space2
            verticalCenter: parent.verticalCenter
        }
        text: pill.label
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        font.letterSpacing: 1.2
        color: pill.on ? Theme.accent : Theme.textDim
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.toggled()
    }
}
