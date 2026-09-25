import QtQuick
import Cadenza

/// The playhead, drawn by hand rather than with QtQuick's Slider so it can look
/// like the rest of the interface.
///
/// While the pointer is down the bar shows the drag value and stops reading the
/// position that keeps arriving from the player. Without that flag the two
/// fight each other and the handle snaps back under the finger -- the classic
/// bug in every music player's seek bar.
Item {
    id: root

    implicitHeight: 18
    height: implicitHeight
    focus: true
    activeFocusOnTab: true

    /// Where the pointer sits while dragging, as a 0 .. 1 ratio.
    property real dragRatio: 0

    readonly property real playedRatio: controller.player.duration > 0
        ? Math.max(0, Math.min(1, controller.player.position / controller.player.duration))
        : 0
    readonly property real shownRatio: pointer.dragging ? dragRatio : playedRatio
    readonly property bool highlighted: pointer.containsMouse || pointer.dragging

    function ratioAt(x)
    {
        return width > 0 ? Math.max(0, Math.min(1, x / width)) : 0
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 4
        radius: Theme.radiusPill
        color: Theme.surfaceActive

        Rectangle {
            width: parent.width * root.shownRatio
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }

        Rectangle {
            id: handle
            x: parent.width * root.shownRatio - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: 12
            radius: Theme.radiusPill
            color: root.highlighted ? Theme.elevate(Theme.text) : Theme.text
            opacity: root.highlighted ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.duration } }
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        property bool dragging: false

        onPressed: (event) => {
            dragging = true
            root.forceActiveFocus()
            root.dragRatio = root.ratioAt(event.x)
        }
        onPositionChanged: (event) => {
            if (dragging)
                root.dragRatio = root.ratioAt(event.x)
        }
        onReleased: () => {
            if (!dragging)
                return
            dragging = false
            controller.player.seek(root.dragRatio * controller.player.duration)
        }
    }

    Keys.onLeftPressed: (event) => {
        controller.player.seekBy(-5)
        event.accepted = true
    }
    Keys.onRightPressed: (event) => {
        controller.player.seekBy(5)
        event.accepted = true
    }
}
