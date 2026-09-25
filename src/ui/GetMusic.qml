import QtQuick
import QtQuick.Controls
import Cadenza

/// Getting music: what YouTube Music has for what you type, and one button per
/// result that puts it in your library.
///
/// The view is built around the one thing that is easy to get wrong -- where
/// the file ends up. The line under the search field says it, and it follows
/// the sidebar: pick a playlist, and that is the folder the next download
/// lands in.
Item {
    id: view

    /// Fires the search. Each one is a request to a service that owes nothing
    /// to anybody, so it happens when it is asked for and not on every
    /// keystroke the way the library filter does.
    function runSearch()
    {
        const text = search.text.trim()
        if (text.length === 0)
            return
        controller.searchOnline(text)
    }

    /// Where the next download lands, spelled out: the name the sidebar calls
    /// the folder and what that means, or only what it means when the sidebar
    /// has no name for it.
    function saveFolderPhrase()
    {
        if (controller.downloadFolderName.length === 0)
            return Tr.t("download_folder_default")
        return Tr.t("download_folder_selected", { name: controller.downloadFolderName })
    }

    /// The view is one question, so the caret is already in the field when it
    /// comes up: whatever is typed next lands in the search box, without
    /// having to find it first.
    onVisibleChanged: if (visible) search.forceActiveFocus()

    Item {
        id: searchRow
        anchors {
            left: parent.left
            leftMargin: Theme.space4
            right: parent.right
            rightMargin: Theme.space4
            top: parent.top
            topMargin: Theme.topRowMargin
        }
        height: Theme.topRowHeight

        // The status line is the rightmost thing in the row, and it is
        // anchored to the row's own right edge: the button hangs off *it*, and
        // the field off the button. Anchoring in the other direction too --
        // the button against the status text and the status text against the
        // button -- is a cycle, and QML breaks it by dropping one of the two:
        // the field then comes out with a negative width and there is nowhere
        // left to type.
        Text {
            id: resultCount
            anchors {
                right: parent.right
                // The window controls float at the end of this row.
                rightMargin: Theme.windowControlsWidth
                verticalCenter: parent.verticalCenter
            }
            // Bounded so a long message cannot squeeze the field out of the
            // row; what does not fit is elided, not wrapped.
            width: Math.min(implicitWidth, searchRow.width * 0.32)
            horizontalAlignment: Text.AlignRight
            text: controller.searchingOnline
                  ? Tr.t("get_music_searching")
                  : (controller.onlineMessage.length > 0
                     ? controller.onlineMessage
                     : (results.count === 0
                        ? ""
                        : Tr.n("get_music_results", results.count)))
            elide: Text.ElideMiddle
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textFaint
        }

        Rectangle {
            id: submit
            anchors {
                right: resultCount.left
                rightMargin: Theme.space4
                verticalCenter: parent.verticalCenter
            }
            width: 84
            height: Theme.topRowHeight
            radius: Theme.radius
            color: submitPointer.containsMouse ? Theme.accentSoft : "transparent"
            border.width: 1
            border.color: Theme.border
            Behavior on color { ColorAnimation { duration: Theme.duration } }

            Text {
                anchors.centerIn: parent
                text: Tr.t("get_music_search")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: search.text.trim().length > 0 ? Theme.text : Theme.textFaint
            }

            MouseArea {
                id: submitPointer
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: view.runSearch()
            }
        }

        TextField {
            id: search
            anchors {
                left: parent.left
                right: submit.left
                rightMargin: Theme.space2
                verticalCenter: parent.verticalCenter
            }
            height: Theme.topRowHeight
            placeholderText: Tr.t("get_music_hint")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            leftPadding: 34
            rightPadding: Theme.space3
            topPadding: 9
            bottomPadding: 9
            selectByMouse: true
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            onAccepted: view.runSearch()

            // No default chrome: the field is a pane of glass with a border,
            // the same material as the panes it sits between.
            background: Glass {
                paneRadius: Theme.radius
                border.color: search.activeFocus ? Theme.accent : Theme.border
            }
        }

        IconButton {
            anchors {
                left: search.left
                leftMargin: Theme.space2
                verticalCenter: parent.verticalCenter
            }
            size: 20
            iconSize: 16
            glyph: "search"
            interactive: false
            color: Theme.textFaint
        }
    }

    /// Where the next download lands, spelled out: the folder name, which is
    /// what the sidebar calls it, and the path, which is what it is.
    Item {
        id: destination
        anchors {
            left: parent.left
            leftMargin: Theme.space4
            right: parent.right
            rightMargin: Theme.space4
            top: searchRow.bottom
            topMargin: Theme.space3
        }
        height: 18

        Text {
            id: destinationLabel
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: Tr.t("get_music_saves_to")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            font.letterSpacing: 1.2
            color: Theme.textFaint
        }

        Text {
            id: destinationName
            anchors {
                left: destinationLabel.right
                leftMargin: Theme.space2
                verticalCenter: parent.verticalCenter
            }
            text: controller.downloadFolderName
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.accent
        }

        Text {
            anchors {
                left: destinationName.right
                leftMargin: Theme.space2
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            text: controller.downloadFolder
            elide: Text.ElideMiddle
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            color: Theme.textFaint
        }
    }

    ListView {
        id: results
        anchors {
            left: parent.left
            right: parent.right
            top: destination.bottom
            topMargin: Theme.space3
            bottom: parent.bottom
            bottomMargin: Theme.space2
        }
        model: controller.downloads
        clip: true
        reuseItems: true
        spacing: 2

        delegate: DownloadRow {
            width: ListView.view.width
        }

        ScrollBar.vertical: ScrollBar {
            id: resultScrollBar
            policy: ScrollBar.AsNeeded
            implicitWidth: 8

            background: Rectangle {
                implicitWidth: 8
                color: "transparent"
            }

            contentItem: Rectangle {
                implicitWidth: 6
                radius: Theme.radiusPill
                color: resultScrollBar.pressed ? Theme.accentDim : Theme.alpha(Theme.textFaint, 0.7)
            }
        }
    }

    EmptyState {
        anchors.centerIn: results
        visible: results.count === 0
        glyph: controller.searchingOnline ? "search" : "download"
        title: controller.searchingOnline ? Tr.t("get_music_searching") : Tr.t("get_music_empty_title")
        message: controller.searchingOnline
                 ? Tr.t("get_music_empty_source")
                 : (controller.onlineMessage.length > 0
                    ? controller.onlineMessage
                    : Tr.t("get_music_empty_message", { folder: view.saveFolderPhrase() }))
    }
}
