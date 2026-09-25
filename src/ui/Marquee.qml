import QtQuick
import Cadenza

/// A label that scrolls itself when the text is wider than the space it was
/// given, and stands still when it fits. Long track titles therefore stay
/// readable instead of being elided to a fragment.
Item {
    id: root

    property string text: ""
    property font font: Qt.application.font
    property color color: Theme.text
    /// Pixels per second the line travels at.
    property int speed: 34
    /// How long the line rests at each end before moving again.
    property int rest: 1600

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight
    width: implicitWidth
    height: implicitHeight
    clip: true

    readonly property real travel: Math.max(0, label.implicitWidth - width)
    readonly property bool scrolling: travel > 0 && visible

    Text {
        id: label
        text: root.text
        font: root.font
        color: root.color
        y: (root.height - implicitHeight) / 2
    }

    SequentialAnimation {
        running: root.scrolling
        loops: Animation.Infinite

        PropertyAction { target: label; property: "x"; value: 0 }
        PauseAnimation { duration: root.rest }
        NumberAnimation {
            target: label
            property: "x"
            to: -root.travel
            duration: Math.max(900, root.travel / root.speed * 1000)
            easing.type: Easing.InOutSine
        }
        PauseAnimation { duration: root.rest }
    }

    // The text can become short while it is scrolled, so put it back when the
    // animation stops owning its position.
    onScrollingChanged: if (!scrolling) label.x = 0
}
