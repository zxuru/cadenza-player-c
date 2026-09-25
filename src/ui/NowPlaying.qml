import QtQuick
import Cadenza

/// The hero view: the artwork, the track's metadata underneath, and the lyrics
/// beside them when the pane is showing. The blurred cover behind all of this
/// belongs to the window -- see `Backdrop` -- so nothing here draws one.
Item {
    id: view

    readonly property bool hasTrack: controller.player.index >= 0

    /// The pane only makes sense with a track to sing along to.
    readonly property bool showLyrics: controller.lyricsVisible && view.hasTrack

    /// The column the artwork and the metadata live in: all of the view when
    /// the lyrics are hidden, a little under half of it when they are not.
    readonly property real heroWidth: view.showLyrics
        ? Math.max(300, Math.round(view.width * 0.42))
        : view.width

    readonly property real artSide: Math.max(120,
        Math.min(view.showLyrics ? 210 : 300,
            Math.min(view.heroWidth - Theme.space6 * 2, view.height - 260)))

    /// Rendered at twice its size, so the baked rounding is exact on a
    /// fractional scale factor and the picture stays sharp on a dense display.
    readonly property int artPixels: Math.max(1, Math.round(view.artSide * 2))

    /// The title, falling back to the file's own name when the tags are bare.
    /// The player answers with both, so the transport and this view cannot
    /// name the same track differently.
    readonly property string currentTitle: controller.player.displayTitle

    /// The control that brings the pane in and takes it away. It sits here as
    /// well as in the settings because this is where the pane is, and where
    /// wanting it or not wanting it is felt.
    TogglePill {
        anchors {
            right: parent.right
            // The window controls float at the end of this row.
            rightMargin: Theme.windowControlsWidth
            top: parent.top
            topMargin: Theme.topRowMargin + (Theme.topRowHeight - height) / 2
        }
        label: Tr.t("lyrics_toggle")
        glyph: "lyrics"
        on: controller.lyricsVisible
        visible: view.hasTrack
        onToggled: controller.lyricsVisible = !controller.lyricsVisible
    }

    Item {
        id: heroColumn
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: view.heroWidth
        visible: view.hasTrack

        Column {
            id: hero
            anchors.centerIn: parent
            width: Math.min(heroColumn.width - Theme.space6 * 2, 640)
            height: implicitHeight
            spacing: Theme.space4

            Item {
                id: artFrame
                anchors.horizontalCenter: parent.horizontalCenter
                width: view.artSide
                height: view.artSide

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusLarge
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    IconButton {
                        anchors.centerIn: parent
                        size: 56
                        iconSize: 48
                        glyph: "note"
                        interactive: false
                        color: Theme.textFaint
                    }
                }

                // The picture arrives already cut to its radius, so nothing
                // here needs to mask it: on a backend where shaders do not run
                // the artwork is still the artwork.
                Image {
                    anchors.fill: parent
                    source: controller.coverUrl(controller.player.path, view.artPixels,
                                                view.artPixels, 0, Theme.radiusLarge * 2)
                    sourceSize: Qt.size(view.artPixels, view.artPixels)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: true
                    visible: status === Image.Ready
                }
            }

            Marquee {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(hero.width, implicitWidth)
                text: view.currentTitle
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHero
                font.bold: true
                color: Theme.text
            }

            Text {
                width: hero.width
                text: controller.player.artist
                visible: text.length > 0
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontRow
                color: Theme.text
            }

            Text {
                width: hero.width
                text: controller.player.album
                visible: text.length > 0
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                color: Theme.textDim
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: streamLabel.implicitWidth + Theme.space4
                height: 24
                radius: Theme.radiusPill
                color: Theme.surfaceActive
                visible: controller.player.streamInfo.length > 0

                Text {
                    id: streamLabel
                    anchors.centerIn: parent
                    text: controller.player.streamInfo
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTiny
                    color: Theme.textFaint
                }
            }
        }
    }

    LyricsPane {
        anchors {
            left: heroColumn.right
            leftMargin: Theme.space5
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            // Below the row the controls and the lyrics toggle share, so the
            // pane's own top edge does not run under them.
            topMargin: Theme.topRowMargin + Theme.topRowHeight
        }
        visible: view.showLyrics
    }

    EmptyState {
        anchors.centerIn: parent
        visible: !view.hasTrack
        glyph: "note"
        title: Tr.t("now_playing_empty_title")
        message: Tr.t("now_playing_empty_message")
    }
}
