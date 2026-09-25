import QtQuick
import Cadenza

/// The transport: what is playing on the left, controls in the middle, output
/// settings on the right. The other pane of glass in the window.
Glass {
    id: bar

    /// True once the queue has run off its end, so play restarts instead of
    /// resuming a finished track.
    property bool ended: false

    /// Asks for the hero view. What is playing is what it shows, and the
    /// artwork here -- the one thing in the transport that is the track itself
    /// rather than a control -- is how it is asked for, so the rail needs no
    /// row of its own for it.
    signal expandRequested()

    implicitHeight: 88
    height: implicitHeight

    /// The whole of ReplayGain: one button that cycles the three modes the
    /// player knows, rather than three segments spelling them out. A setting
    /// that is read far more often than it is set does not need its every
    /// option on screen, and two of the three say what they are with a mark --
    /// a loop with a 1 in it for the track, a record for the album -- while
    /// off says so in the only way left to it, in words. The tooltip still
    /// names the mode in full. The width is fixed so the volume beside it does
    /// not move when the mode does.
    component GainButton: Rectangle {
        id: gainButton

        /// What the player is set to: `no`, `track` or `album`, in the order
        /// the button cycles them.
        property string mode: "no"

        /// Whether it is doing anything, which is what the fill says.
        readonly property bool on: gainButton.mode !== "no"

        /// What the mode is called in full, for the tooltip.
        readonly property string modeKey: gainButton.mode === "track"
                                          ? "replaygain_track"
                                          : (gainButton.mode === "album"
                                             ? "replaygain_album"
                                             : "replaygain_off")

        /// Moves the player on to the next mode: off, then track, then album,
        /// and back to off.
        function cycle()
        {
            controller.player.replayGain = gainButton.mode === "no"
                                           ? "track"
                                           : (gainButton.mode === "track" ? "album" : "no")
        }

        width: 34
        height: 28
        radius: Theme.radiusSmall
        color: gainButton.on
               ? Theme.accent
               : (pointer.containsMouse ? Theme.surfaceHover : "transparent")
        border.width: gainButton.on ? 0 : 1
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.duration } }

        Text {
            anchors.centerIn: parent
            visible: !gainButton.on
            text: Tr.t("replaygain_off_short")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            font.letterSpacing: 1.2
            color: Theme.textDim
        }

        IconButton {
            anchors.centerIn: parent
            size: 28
            iconSize: 19
            interactive: false
            visible: gainButton.on
            glyph: gainButton.mode === "track" ? "repeatone" : "album"
            color: Theme.accentText
        }

        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: gainButton.cycle()
        }

        // A tooltip, drawn the way IconButton draws its own, and above the
        // button rather than below: the transport is at the foot of the
        // window, and there is no room under it.
        Rectangle {
            anchors { left: parent.left; bottom: parent.top; bottomMargin: Theme.space1 }
            width: gainTooltip.implicitWidth + Theme.space2 * 2
            height: 22
            radius: Theme.radiusSmall
            color: Theme.alpha(Theme.bg, 0.94)
            border.width: 1
            border.color: Theme.border
            visible: pointer.containsMouse
            z: 100

            Text {
                id: gainTooltip
                anchors.centerIn: parent
                text: Tr.t("replaygain_tooltip", { mode: Tr.t(gainButton.modeKey) })
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.text
            }
        }
    }

    /// How the queue is played, in one button with three states, the way the
    /// ReplayGain button beside the volume has three: off, the list that is
    /// playing, or the whole library. The fill says which, growing from an
    /// outline to a veil to the accent, so the state is legible without the
    /// tooltip -- which still names it in full, and names the list too, because
    /// the list a shuffle draws on is the one that is playing rather than the
    /// one the sidebar happens to be showing.
    component ShuffleButton: Rectangle {
        id: shuffleButton

        /// What the player is set to: `off`, `list` or `library`, in the order
        /// the button cycles them.
        property string mode: "off"

        /// The mode's name in full, for the tooltip.
        readonly property string modeName: shuffleButton.mode === "library"
                                           ? Tr.t("transport_shuffle_library")
                                           : (shuffleButton.mode === "list"
                                              ? controller.playbackSource
                                              : Tr.t("transport_shuffle_off"))

        width: 34
        height: 28
        radius: Theme.radiusSmall
        color: shuffleButton.mode === "library"
               ? Theme.accent
               : (shuffleButton.mode === "list"
                  ? Theme.accentSoft
                  : (shufflePointer.containsMouse ? Theme.surfaceHover : "transparent"))
        border.width: shuffleButton.mode === "off" ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.duration } }

        IconButton {
            anchors.centerIn: parent
            size: 28
            iconSize: 19
            interactive: false
            glyph: "shuffle"
            color: shuffleButton.mode === "library"
                   ? Theme.accentText
                   : (shuffleButton.mode === "list" ? Theme.accent : Theme.textDim)
        }

        MouseArea {
            id: shufflePointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: controller.cycleShuffle()
        }

        // A tooltip, drawn the way IconButton draws its own, and above the
        // button rather than below: the transport is at the foot of the
        // window, and there is no room under it.
        Rectangle {
            anchors { left: parent.left; bottom: parent.top; bottomMargin: Theme.space1 }
            width: shuffleTooltip.implicitWidth + Theme.space2 * 2
            height: 22
            radius: Theme.radiusSmall
            color: Theme.alpha(Theme.bg, 0.94)
            border.width: 1
            border.color: Theme.border
            visible: shufflePointer.containsMouse
            z: 100

            Text {
                id: shuffleTooltip
                anchors.centerIn: parent
                text: Tr.t("transport_shuffle_tooltip", { mode: shuffleButton.modeName })
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.text
            }
        }
    }

    function toggle()
    {
        if (controller.player.index < 0) {
            controller.playRow(0)
            return
        }
        if (bar.ended) {
            bar.ended = false
            controller.player.restart()
            return
        }
        controller.player.toggle()
    }

    Connections {
        target: controller.player
        function onFinished() { bar.ended = true }
        function onPathChanged() { bar.ended = false }
    }

    Item {
        id: nowBlock
        anchors { left: parent.left; leftMargin: Theme.space4; verticalCenter: parent.verticalCenter }
        width: 240
        height: 56

        Rectangle {
            id: thumbFrame
            width: 56
            height: 56
            radius: Theme.radiusThumb
            color: Theme.surfaceActive
            clip: true

            IconButton {
                anchors.centerIn: parent
                size: 22
                iconSize: 20
                glyph: "note"
                interactive: false
                color: Theme.textFaint
                visible: thumb.status !== Image.Ready
            }

            Image {
                id: thumb
                anchors.fill: parent
                source: controller.coverUrl(controller.player.path, 112, 112, 0,
                                            Theme.radiusThumb * 2)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                smooth: true
                visible: status === Image.Ready
            }

            // The artwork is a way in rather than only a picture: this is the
            // ring that says so while the pointer is on the block.
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusThumb
                color: "transparent"
                border.width: 1
                border.color: Theme.accent
                opacity: nowPointer.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.duration } }
            }
        }

        Column {
            id: infoBlock
            anchors {
                left: thumbFrame.right
                leftMargin: Theme.space3
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            height: implicitHeight
            spacing: Theme.space1
            visible: controller.player.error.length === 0

            Row {
                id: titleRow
                width: infoBlock.width
                height: 22
                spacing: Theme.space2

                Rectangle {
                    id: playingDot
                    anchors.verticalCenter: parent.verticalCenter
                    width: 6
                    height: 6
                    radius: Theme.radiusPill
                    color: Theme.accent
                    visible: controller.player.playing
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: titleRow.width
                           - (playingDot.visible ? playingDot.width + titleRow.spacing : 0)
                    text: controller.player.displayTitle.length > 0
                          ? controller.player.displayTitle
                          : Tr.t("now_playing_empty_title")
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontRow
                    font.bold: true
                    color: controller.player.displayTitle.length > 0 ? Theme.text : Theme.textDim
                }
            }

            Text {
                width: infoBlock.width
                text: controller.player.artist
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textDim
            }
        }

        Text {
            anchors {
                left: thumbFrame.right
                leftMargin: Theme.space3
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            visible: controller.player.error.length > 0
            text: controller.player.error
            color: Theme.danger
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }

        // The whole block is the target rather than only the picture: the name
        // of what is playing belongs to that track as much as its cover does.
        MouseArea {
            id: nowPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: bar.expandRequested()
        }

        Rectangle {
            anchors { left: parent.left; bottom: parent.top; bottomMargin: Theme.space1 }
            width: nowTooltip.implicitWidth + Theme.space2 * 2
            height: 22
            radius: Theme.radiusSmall
            color: Theme.alpha(Theme.bg, 0.94)
            border.width: 1
            border.color: Theme.border
            visible: nowPointer.containsMouse
            z: 100

            Text {
                id: nowTooltip
                anchors.centerIn: parent
                text: Tr.t("nav_now_playing")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.text
            }
        }
    }

    Row {
        id: rightBlock
        anchors { right: parent.right; rightMargin: Theme.space4; verticalCenter: parent.verticalCenter }
        width: implicitWidth
        height: 28
        spacing: Theme.space2

        // ReplayGain leads the output controls, because that is what it is:
        // how loud the file is taken to be. It used to be at the foot of the
        // rail, where it was read far more often than it was set; it is now
        // one button that says which way it is set, rather than three saying
        // which ways there are.
        GainButton {
            mode: controller.player.replayGain
        }

        VolumeControl {}

        Rectangle {
            width: 86
            height: 28
            radius: Theme.radiusSmall
            color: controller.player.exclusive
                   ? Theme.surfaceActive
                   : (exclusivePointer.containsMouse ? Theme.surfaceHover : "transparent")
            border.width: 1
            border.color: controller.player.exclusive ? Theme.accent : Theme.border
            Behavior on color { ColorAnimation { duration: Theme.duration } }

            Text {
                anchors.centerIn: parent
                text: Tr.t("transport_exclusive")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                font.letterSpacing: 1.2
                color: controller.player.exclusive ? Theme.accent : Theme.textDim
            }

            MouseArea {
                id: exclusivePointer
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: controller.player.exclusive = !controller.player.exclusive
            }
        }
    }

    Item {
        anchors {
            left: nowBlock.right
            leftMargin: Theme.space4
            right: rightBlock.left
            rightMargin: Theme.space4
            top: parent.top
            bottom: parent.bottom
        }

        IconButton {
            id: playButton
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.space3 }
            size: 40
            iconSize: 18
            round: true
            accent: true
            glyph: controller.player.playing ? "pause" : "play"
            tooltip: controller.player.playing ? Tr.t("transport_pause") : Tr.t("transport_play")
            onClicked: bar.toggle()
        }

        IconButton {
            id: previousButton
            anchors {
                right: playButton.left
                rightMargin: Theme.space3
                verticalCenter: playButton.verticalCenter
            }
            size: 28
            iconSize: 17
            glyph: "previous"
            tooltip: Tr.t("transport_previous")
            onClicked: {
                bar.ended = false
                controller.player.previous()
            }
        }

        ShuffleButton {
            mode: controller.shuffle
            anchors {
                right: previousButton.left
                rightMargin: Theme.space3
                verticalCenter: playButton.verticalCenter
            }
        }

        IconButton {
            anchors {
                left: playButton.right
                leftMargin: Theme.space3
                verticalCenter: playButton.verticalCenter
            }
            size: 28
            iconSize: 17
            glyph: "next"
            tooltip: Tr.t("transport_next")
            onClicked: {
                bar.ended = false
                controller.player.next()
            }
        }

        Row {
            id: seekRow
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: Theme.space2 }
            height: 18
            spacing: Theme.space2

            Text {
                id: elapsedLabel
                width: 44
                height: seekRow.height
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: controller.formatTime(controller.player.position)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textFaint
            }

            SeekBar {
                height: seekRow.height
                width: seekRow.width - elapsedLabel.width - totalLabel.width - seekRow.spacing * 2
            }

            Text {
                id: totalLabel
                width: 44
                height: seekRow.height
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: controller.formatTime(controller.player.duration)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textFaint
            }
        }
    }
}
