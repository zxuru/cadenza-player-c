import QtQuick
import QtQuick.Controls
import Cadenza

/// The lyrics of the track that is playing, on a pane of its own: a column of
/// text over a blurred cover needs a surface under it to stay readable.
///
/// When the source timed the lyrics, the line being sung is picked out in the
/// accent colour and the list follows the playhead; clicking a line seeks to
/// it. When the source did not -- a `USLT` tag, a `.txt` file, an untimed
/// LRCLIB entry -- the same lines are shown without the follow, because there
/// is nothing to follow.
Glass {
    id: pane

    paneRadius: Theme.radiusPanel

    readonly property var lyrics: controller.lyrics
    readonly property bool hasLines: pane.lyrics.count > 0
    /// Every line is the same height on purpose: the follow below needs the
    /// position of a line that has not been built yet, and arithmetic beats
    /// asking the view for an item it does not have.
    readonly property int lineHeight: 48
    readonly property int lineStep: pane.lineHeight + lineList.spacing

    /// A line of lyrics. The active one is the accent colour and a shade
    /// brighter; the rest sit back until they are sung.
    component LyricLine: Item {
        id: line

        /// Row of the model, handed in by the view.
        required property int index
        /// The words, handed in by the model.
        required property string text

        /// Kept as properties rather than read from the pane: an inline
        /// component has no access to the ids of the file it lives in.
        property int activeLine: -1
        property bool timed: false
        property int rowHeight: 48

        readonly property bool active: line.index === line.activeLine

        width: ListView.view ? ListView.view.width : 0
        height: line.rowHeight

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.radius
            color: pointer.containsMouse && line.timed
                   ? Theme.surfaceHover
                   : (line.active ? Theme.accentSoft : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.duration } }
        }

        Text {
            anchors {
                left: parent.left
                right: parent.right
                leftMargin: Theme.space3
                rightMargin: Theme.space3
                verticalCenter: parent.verticalCenter
            }
            text: line.text
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontRow
            font.bold: line.active
            color: line.active ? Theme.accent : Theme.textDim
            Behavior on color { ColorAnimation { duration: Theme.duration } }
        }

        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: line.timed
            enabled: line.timed
            cursorShape: Qt.PointingHandCursor
            onClicked: controller.playLyricLine(line.index)
        }
    }

    ListView {
        id: lineList
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: footer.top
            leftMargin: Theme.space2
            rightMargin: Theme.space2
            topMargin: Theme.space2
        }
        model: controller.lyrics
        clip: true
        reuseItems: true
        spacing: Theme.space1
        visible: pane.hasLines

        delegate: LyricLine {
            activeLine: pane.lyrics.activeIndex
            timed: pane.lyrics.synced
            rowHeight: pane.lineHeight
        }

        ScrollBar.vertical: ScrollBar {
            id: lyricScrollBar
            policy: ScrollBar.AsNeeded
            implicitWidth: 8

            background: Rectangle {
                implicitWidth: 8
                color: "transparent"
            }

            contentItem: Rectangle {
                implicitWidth: 6
                radius: Theme.radiusPill
                color: lyricScrollBar.pressed ? Theme.accentDim : Theme.alpha(Theme.textFaint, 0.7)
            }
        }

        // Only ever started by the playhead: a drag sets `contentY` itself, and
        // an animation running at the same time would fight it.
        NumberAnimation {
            id: follow
            target: lineList
            property: "contentY"
            duration: 420
            easing.type: Easing.OutCubic
        }

        onMovementStarted: follow.stop()
        onHeightChanged: pane.followActiveLine()
        onVisibleChanged: {
            if (visible)
                pane.followActiveLine()
        }

        Connections {
            target: pane.lyrics

            function onActiveIndexChanged()
            {
                pane.followActiveLine()
            }
        }
    }

    /// Centres the line being sung, clamped to the content, so the follow
    /// never leaves a gap at either end.
    function followActiveLine()
    {
        const index = pane.lyrics.activeIndex
        if (index < 0 || !pane.hasLines || !lineList.visible)
            return

        // Nothing to follow while the user is reading somewhere else: the next
        // line change brings the view back on its own.
        if (lineList.moving || lineList.flicking)
            return

        const wanted = index * pane.lineStep - (lineList.height - pane.lineHeight) / 2
        const limit = Math.max(0, lineList.contentHeight - lineList.height)
        const target = Math.max(0, Math.min(limit, wanted))

        if (Math.abs(target - lineList.contentY) < 1)
            return

        follow.stop()
        follow.from = lineList.contentY
        follow.to = target
        follow.start()
    }

    Item {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: Theme.space4
            rightMargin: Theme.space3
            bottomMargin: Theme.space3
        }
        height: 28

        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            visible: pane.hasLines && pane.lyrics.sourceKey.length > 0
            text: Tr.t("lyrics_from", { source: Tr.t(pane.lyrics.sourceKey) })
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            color: Theme.textFaint
        }

        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            height: 28
            spacing: Theme.space2

            IconButton {
                size: 28
                iconSize: 16
                glyph: "refresh"
                visible: controller.onlineLyrics && !pane.hasLines && !pane.lyrics.loading
                tooltip: Tr.t("lyrics_retry")
                onClicked: controller.retryLyrics()
            }

            // The one control here that leaves the machine. It sits next to the
            // lyrics it fetches as well as in the settings, because this is
            // where its absence is felt.
            TogglePill {
                label: Tr.t("lyrics_online")
                on: controller.onlineLyrics
                onToggled: controller.onlineLyrics = !controller.onlineLyrics
            }
        }
    }

    /// What stands where the list would be, until there is something to put in
    /// it.
    Column {
        anchors.centerIn: lineList
        width: Math.min(lineList.width - Theme.space4, 340)
        spacing: Theme.space3
        visible: !pane.hasLines

        IconButton {
            anchors.horizontalCenter: parent.horizontalCenter
            size: 40
            iconSize: 32
            glyph: "lyrics"
            interactive: false
            color: Theme.textFaint
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: {
                if (pane.lyrics.loading)
                    return Tr.t("lyrics_loading")
                if (pane.lyrics.instrumental)
                    return Tr.t("lyrics_instrumental")
                return Tr.t("lyrics_none")
            }
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontRow
            color: Theme.text
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: !pane.lyrics.loading && !pane.lyrics.instrumental
            text: controller.onlineLyrics
                  ? Tr.t("lyrics_none_online")
                  : Tr.t("lyrics_none_offline")
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textDim
        }
    }
}
