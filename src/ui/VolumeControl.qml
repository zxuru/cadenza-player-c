import QtQuick
import Cadenza

/// Volume with its mute glyph, built like SeekBar so the two read as one
/// control. It draws its own track rather than reusing SeekBar because it
/// drives a 0 .. 100 volume instead of the playhead.
Item {
    id: root

    implicitWidth: 128
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    /// Where the pointer sits while dragging, as a 0 .. 1 ratio.
    property real dragRatio: 0

    readonly property real shownRatio: pointer.dragging
        ? dragRatio
        : Math.max(0, Math.min(1, controller.player.volume / 100))
    readonly property bool highlighted: pointer.containsMouse || pointer.dragging

    function applyPointer(x)
    {
        var ratio = slider.width > 0 ? Math.max(0, Math.min(1, x / slider.width)) : 0
        dragRatio = ratio
        controller.player.volume = Math.round(ratio * 100)
    }

    IconButton {
        id: muteButton
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        size: 24
        iconSize: 18
        glyph: controller.player.muted ? "mute" : "volume"
        active: controller.player.muted
        tooltip: controller.player.muted ? Tr.t("transport_unmute") : Tr.t("transport_mute")
        onClicked: controller.player.muted = !controller.player.muted
    }

    Item {
        id: slider
        anchors { left: muteButton.right; leftMargin: Theme.space2; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 18

        Rectangle {
            id: track
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: 4
            radius: Theme.radiusPill
            color: Theme.surfaceActive

            Rectangle {
                width: parent.width * root.shownRatio
                height: parent.height
                radius: parent.radius
                color: controller.player.muted ? Theme.textFaint : Theme.accent
            }

            Rectangle {
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
                root.applyPointer(event.x)
            }
            onPositionChanged: (event) => {
                if (dragging)
                    root.applyPointer(event.x)
            }
            onReleased: () => {
                dragging = false
            }
        }
    }
}
