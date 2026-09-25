import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Cadenza

/// The navigation rail: the wordmark and the views at the top, the subfolders
/// of the music folder as playlists under them. The hero view is not one of
/// its rows -- the artwork in the transport opens it -- and nothing that is set
/// once and then left alone lives here either: the folder, the lyrics and the
/// language are on the settings page, where their controls have the room they
/// want. A pane of glass floating inside the window, so what is playing shows
/// through it.
///
/// The heading over the playlists carries the two things that act on them --
/// showing the music folder in the desktop's file manager, and walking it
/// again -- as small faint glyphs, because a rail is for going somewhere
/// rather than for buttons.
Glass {
    id: sidebar

    /// 0 shows the library, 2 the search for music to download, 3 the settings.
    /// 1, the now playing view, is opened from the transport.
    property int view: 0

    signal viewRequested(int target)

    implicitWidth: 216
    width: implicitWidth

    component NavRow: Item {
        id: navRow

        property string label: ""
        property bool selected: false

        signal activated()

        implicitHeight: 38
        height: implicitHeight

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: navRow.selected
                   ? Theme.surfaceActive
                   : (navPointer.containsMouse ? Theme.surfaceHover : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.duration } }
        }

        Rectangle {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: 3
            height: 16
            radius: Theme.radiusPill
            color: Theme.accent
            visible: navRow.selected
        }

        Text {
            anchors { left: parent.left; leftMargin: Theme.space3; verticalCenter: parent.verticalCenter }
            text: navRow.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.bold: navRow.selected
            color: navRow.selected ? Theme.text : Theme.textDim
        }

        MouseArea {
            id: navPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: navRow.activated()
        }
    }

    /// Small uppercase section label.
    component SectionLabel: Text {
        property string title: ""
        text: title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        font.letterSpacing: 1.4
        color: Theme.textFaint
    }

    Column {
        id: upper
        anchors {
            left: parent.left
            leftMargin: Theme.space3
            right: parent.right
            rightMargin: Theme.space3
            top: parent.top
            topMargin: Theme.space4
        }
        height: implicitHeight
        spacing: Theme.space1

        Row {
            id: wordmark
            width: upper.width
            height: 30
            spacing: Theme.space1

            // The application's own icon, beside its name: the same drawing the
            // launcher and the window carry, bound into the binary from
            // packaging/cadenza.svg. Rasterised at the size it is drawn in, so
            // it is sharp on a scaled display rather than scaled up from 512.
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                source: "qrc:/icons/cadenza.svg"
                sourceSize: Qt.size(width * Screen.devicePixelRatio,
                                    height * Screen.devicePixelRatio)
                smooth: true
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Cadenza"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -8
                width: 6
                height: 6
                radius: Theme.radiusPill
                color: Theme.accent
            }
        }

        Item { width: 1; height: Theme.space3 }

        NavRow {
            width: upper.width
            label: Tr.t("nav_library")
            selected: sidebar.view === 0
            onActivated: sidebar.viewRequested(0)
        }

        NavRow {
            width: upper.width
            label: Tr.t("nav_get_music")
            selected: sidebar.view === 2
            onActivated: sidebar.viewRequested(2)
        }

        NavRow {
            width: upper.width
            label: Tr.t("nav_settings")
            selected: sidebar.view === 3
            onActivated: sidebar.viewRequested(3)
        }
    }

    SectionLabel {
        id: playlistsLabel
        anchors {
            left: parent.left
            leftMargin: Theme.space3
            top: upper.bottom
            topMargin: Theme.space5
        }
        title: Tr.t("sidebar_playlists")
    }

    // What can be done to the folder the playlists are cut from: open it in the
    // desktop's own file manager, or walk it again. Both sit on the heading
    // over the list they act on, and both stay faint -- brighter only under the
    // pointer -- so the rail is still the playlists first.
    Row {
        anchors {
            right: parent.right
            rightMargin: Theme.space3
            verticalCenter: playlistsLabel.verticalCenter
        }
        spacing: Theme.space1

        // The music folder itself, for everything the player does not do to a
        // file: moving it, tagging it, dropping one in.
        IconButton {
            size: 22
            iconSize: 14
            glyph: "folder"
            color: hovered ? Theme.text : Theme.textFaint
            tooltip: Tr.t("sidebar_open_folder")
            onClicked: controller.openRootFolder()
        }

        // Walks the root again, so a file that arrived from outside the player
        // is in the index without a relaunch. It turns while the walk runs,
        // which is the only sign it gives of being busy.
        IconButton {
            size: 22
            iconSize: 14
            glyph: "refresh"
            color: hovered ? Theme.text : Theme.textFaint
            spinning: controller.scanning
            tooltip: Tr.t("sidebar_reindex")
            onClicked: controller.rescan()
        }
    }

    // The subfolders of the music folder, one entry each. They are a flat list
    // rather than a tree because that is what the folder layout actually is --
    // an artist folder holding album folders still reads as one playlist per
    // artist, and picking it plays everything underneath it.
    ListView {
        id: playlistList
        anchors {
            left: parent.left
            leftMargin: Theme.space2
            right: parent.right
            rightMargin: Theme.space2
            top: playlistsLabel.bottom
            topMargin: Theme.space2
            bottom: parent.bottom
            bottomMargin: Theme.space3
        }
        clip: true
        spacing: 1
        model: controller.folders

        ScrollBar.vertical: ScrollBar {
            id: playlistScrollBar
            policy: ScrollBar.AsNeeded
            implicitWidth: 6

            background: Rectangle { implicitWidth: 6; color: "transparent" }

            contentItem: Rectangle {
                implicitWidth: 4
                radius: Theme.radiusPill
                color: playlistScrollBar.pressed ? Theme.accentDim : Theme.alpha(Theme.textFaint, 0.7)
            }
        }

        delegate: Item {
            id: playlistRow

            required property var modelData
            required property int index

            // The empty path is the "All music" entry, which is the state of
            // having no folder restriction at all.
            readonly property bool active: controller.selectedFolder === playlistRow.modelData.path

            // A playlist is a folder, so renaming one renames the folder: the
            // field writes the name it is to have and the rail follows the
            // answer, which is nothing written when the folder took the name.
            property bool renaming: false
            // Key of the string saying why the folder would not take the name.
            property string renameProblem: ""

            function beginRename()
            {
                renameField.text = playlistRow.modelData.name
                renameProblem = ""
                renaming = true
            }

            function endRename()
            {
                renaming = false
                renameProblem = ""
            }

            function commitRename()
            {
                if (!renaming)
                    return

                const problem = controller.renamePlaylist(playlistRow.modelData.path,
                                                          renameField.text)
                if (problem.length > 0) {
                    renameProblem = problem
                    renameField.forceActiveFocus()
                    return
                }
                endRename()
            }

            width: playlistList.width
            // A refused name is said under the field that was refused, and the
            // row grows the one line it needs to say it in.
            height: renaming && renameProblem.length > 0 ? 48 : 32

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                // The chosen playlist wears the accent as a veil, so the
                // selection is the same violet as everything else that is on.
                color: playlistRow.active
                       ? Theme.accentSoft
                       : (rowPointer.containsMouse ? Theme.surfaceHover : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.duration } }
            }

            Rectangle {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: 2
                height: 14
                radius: Theme.radiusPill
                color: Theme.accent
                visible: playlistRow.active
            }

            Text {
                anchors {
                    left: parent.left
                    leftMargin: Theme.space3
                    right: folderCount.left
                    rightMargin: Theme.space2
                    verticalCenter: parent.verticalCenter
                }
                text: playlistRow.modelData.name
                visible: !playlistRow.renaming
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.bold: playlistRow.active
                color: playlistRow.active ? Theme.text : Theme.textDim
            }

            Text {
                id: folderCount
                anchors {
                    right: parent.right
                    rightMargin: Theme.space2
                    verticalCenter: parent.verticalCenter
                }
                text: playlistRow.modelData.count
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: playlistRow.active ? Theme.accent : Theme.textFaint
            }

            MouseArea {
                id: rowPointer
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    controller.selectFolder(playlistRow.modelData.path)
                    sidebar.viewRequested(0)
                }
                // The other way into the field, for the hand that expects a
                // row to answer a second click.
                onDoubleClicked: if (!playlistRow.renaming) playlistRow.beginRename()
            }

            // The way in: shown on the row the pointer is over, and kept on
            // screen while the field is open. "All music" is the library itself
            // rather than a folder, so it has no name to change.
            IconButton {
                id: renameButton
                anchors {
                    right: folderCount.left
                    rightMargin: Theme.space1
                    verticalCenter: parent.verticalCenter
                }
                size: 22
                iconSize: 14
                glyph: "pencil"
                tooltip: Tr.t("playlist_rename")
                visible: playlistRow.modelData.path.length > 0
                         && (rowPointer.containsMouse || playlistRow.renaming)
                onClicked: playlistRow.beginRename()
            }

            // The name being written, in the place the name was: the row keeps
            // its shape, so it is not the field that has to be found first.
            TextField {
                id: renameField
                anchors {
                    left: parent.left
                    leftMargin: Theme.space2
                    right: renameButton.left
                    rightMargin: Theme.space1
                    verticalCenter: parent.verticalCenter
                }
                height: 24
                visible: playlistRow.renaming
                color: Theme.text
                leftPadding: Theme.space1
                rightPadding: Theme.space1
                topPadding: 2
                bottomPadding: 2
                selectByMouse: true
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                onAccepted: playlistRow.commitRename()
                // The caret is already in the field, and the name is already
                // picked out: typing replaces it, which is what a rename is.
                onVisibleChanged: if (visible) {
                    forceActiveFocus()
                    selectAll()
                }
                // Clicking away keeps what was typed -- the same as Enter. A
                // refused name is the one thing that keeps the field open, and
                // it takes the focus back to say so.
                onActiveFocusChanged: if (!activeFocus) playlistRow.commitRename()
                onTextEdited: playlistRow.renameProblem = ""
                Keys.onEscapePressed: playlistRow.endRename()

                background: Glass {
                    paneRadius: Theme.radiusSmall
                    border.color: renameField.activeFocus ? Theme.accent : Theme.border
                }
            }

            Text {
                id: renameProblemText
                anchors {
                    left: parent.left
                    leftMargin: Theme.space3
                    right: parent.right
                    rightMargin: Theme.space2
                    top: renameField.bottom
                    topMargin: Theme.space1
                }
                text: Tr.t(playlistRow.renameProblem, { name: renameField.text.trim() })
                visible: playlistRow.renaming && playlistRow.renameProblem.length > 0
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.danger
            }
        }
    }
}
