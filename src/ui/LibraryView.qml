import QtQuick
import QtQuick.Controls
import Cadenza

/// The library: what to browse, what to search for, and the list itself --
/// tracks, or the artists, albums and genres they are grouped by.
Item {
    id: view

    // Column metrics. The row delegates and the header both read them, so the
    // labels cannot drift away from the columns they name.
    readonly property int indexColumnWidth: 28
    readonly property int artSize: 44
    readonly property int groupArtSize: 56
    readonly property int timeColumnWidth: 56
    readonly property int gutter: Theme.space3

    // The album column is the flexible one: it takes a share of what the fixed
    // columns leave over, so a wide window shows more of the album name.
    readonly property int albumColumnWidth: Math.max(120, Math.round(
        (listArea.width - indexColumnWidth - artSize - timeColumnWidth
         - gutter * 4 - Theme.space2 * 2) * 0.42))

    readonly property bool empty: controller.trackCount === 0 && !controller.scanning
    readonly property bool searching: controller.filter.length > 0

    /// True while the list is a list of groups rather than of tracks: a grouped
    /// browse mode, with no group opened.
    readonly property bool grouped: controller.browse !== "tracks" && !controller.inGroup

    /// The word the hint and the empty state name the field with, in the
    /// singular. Empty in the flat list, which searches every field at once.
    readonly property string fieldWord: {
        switch (controller.browse) {
        case "artists":
            return Tr.t("library_field_artist")
        case "albums":
            return Tr.t("library_field_album")
        case "genres":
            return Tr.t("library_field_genre")
        default:
            return ""
        }
    }

    /// What the list is narrowed to, which is what the count line names: the
    /// group that was opened, or the playlist the sidebar is on.
    readonly property string scopeName: controller.inGroup
                                            ? controller.groupName
                                            : controller.selectedFolderName

    /// What the count line counts: the tracks of the list, or the groups a
    /// grouped one is made of.
    readonly property string countText: {
        if (!view.grouped)
            return Tr.n("library_tracks", controller.trackCount)

        switch (controller.browse) {
        case "artists":
            return Tr.n("library_group_artists", controller.groups.rowCount)
        case "albums":
            return Tr.n("library_group_albums", controller.groups.rowCount)
        default:
            return Tr.n("library_group_genres", controller.groups.rowCount)
        }
    }

    function emptyMessage()
    {
        if (!view.searching)
            return Tr.t("library_empty_message")

        return view.grouped
            ? Tr.t("library_empty_scoped_message",
                   { field: view.fieldWord.toLowerCase(), query: controller.filter })
            : Tr.t("library_empty_search_message", { query: controller.filter })
    }

    /// Small uppercase label above the columns.
    component HeaderLabel: Text {
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        font.letterSpacing: 1.2
        color: Theme.textFaint
    }

    /// The one scrollbar both lists wear.
    component ListScroll: ScrollBar {
        id: scroll

        policy: ScrollBar.AsNeeded
        implicitWidth: 8

        background: Rectangle {
            implicitWidth: 8
            color: "transparent"
        }

        contentItem: Rectangle {
            implicitWidth: 6
            radius: Theme.radiusPill
            color: scroll.pressed ? Theme.accentDim : Theme.alpha(Theme.textFaint, 0.7)
        }
    }

    /// The way back out of a group, above the list it narrows.
    component GroupTrail: Glass {
        id: trail

        readonly property bool hovered: trailPointer.containsMouse

        width: trailRow.width + Theme.space3 * 2
        height: 28
        paneRadius: Theme.radiusPill
        border.color: trail.hovered ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.duration } }

        Row {
            id: trailRow
            anchors.centerIn: parent
            spacing: Theme.space1

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("library_browse_" + controller.browse)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                font.letterSpacing: 1.2
                color: Theme.textDim
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "·"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                color: Theme.textFaint
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: controller.groupName
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                font.letterSpacing: 1.2
                color: Theme.accent
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                size: 14
                iconSize: 12
                glyph: "close"
                interactive: false
                color: trail.hovered ? Theme.accent : Theme.textFaint
            }
        }

        MouseArea {
            id: trailPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: controller.closeGroup()
        }
    }

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

        LibraryBrowse {
            id: browsePicker
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            mode: controller.browse
            onSelected: (chosen) => controller.browse = chosen
        }

        TextField {
            id: search
            anchors {
                left: browsePicker.right
                leftMargin: Theme.space2
                right: countLabel.left
                rightMargin: Theme.space4
                verticalCenter: parent.verticalCenter
            }
            height: 36
            text: controller.filter
            placeholderText: view.grouped
                ? Tr.t("library_search_hint_field", { field: view.fieldWord.toLowerCase() })
                : Tr.t("library_search_hint")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            leftPadding: 34
            rightPadding: Theme.space3
            topPadding: 9
            bottomPadding: 9
            selectByMouse: true
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            onTextEdited: controller.filter = search.text
            Keys.onEscapePressed: (event) => {
                // Escape backs out of one thing at a time: the list of modes
                // first, then the query itself.
                if (browsePicker.open)
                    browsePicker.close()
                else
                    controller.clearFilter()
                event.accepted = true
            }

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

        Text {
            id: countLabel
            anchors {
                right: parent.right
                // The window controls float at the end of this row.
                rightMargin: Theme.windowControlsWidth
                verticalCenter: parent.verticalCenter
            }
            text: view.scopeName.length > 0
                ? Tr.t("library_in_folder", { tracks: view.countText, folder: view.scopeName })
                : view.countText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textFaint
        }
    }

    Item {
        id: trailRow
        anchors {
            left: parent.left
            leftMargin: Theme.space4
            right: parent.right
            rightMargin: Theme.space4
            top: searchRow.bottom
            topMargin: Theme.space2
        }
        height: controller.inGroup ? trailChip.height : 0
        visible: controller.inGroup

        GroupTrail {
            id: trailChip
            anchors.left: parent.left
        }
    }

    Item {
        id: columnHeader
        anchors {
            left: parent.left
            leftMargin: Theme.space2
            right: parent.right
            rightMargin: Theme.space2
            top: controller.inGroup ? trailRow.bottom : searchRow.bottom
            topMargin: Theme.space2
        }
        height: 24

        // The flat list names its columns; a grouped one names the one thing it
        // is a list of, and the duration beside it.
        HeaderLabel {
            id: headerIndex
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: view.indexColumnWidth
            horizontalAlignment: Text.AlignRight
            text: "#"
            visible: !view.grouped
        }

        // Stands in for the artwork column so the titles line up.
        Item {
            id: headerArt
            anchors { left: headerIndex.right; leftMargin: view.gutter; verticalCenter: parent.verticalCenter }
            width: view.artSize
            height: 1
            visible: !view.grouped
        }

        HeaderLabel {
            anchors {
                left: view.grouped ? parent.left : headerArt.right
                leftMargin: view.grouped ? Theme.space2 : view.gutter
                verticalCenter: parent.verticalCenter
            }
            text: view.grouped
                ? view.fieldWord.toUpperCase()
                : Tr.t("library_column_title")
        }

        HeaderLabel {
            anchors { right: headerTime.left; rightMargin: view.gutter; verticalCenter: parent.verticalCenter }
            width: view.albumColumnWidth
            text: Tr.t("library_column_album")
            visible: !view.grouped
        }

        HeaderLabel {
            id: headerTime
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: view.timeColumnWidth
            horizontalAlignment: Text.AlignRight
            text: Tr.t("library_column_time")
        }
    }

    Item {
        id: listArea
        anchors {
            left: parent.left
            right: parent.right
            top: columnHeader.bottom
            topMargin: Theme.space1
            bottom: parent.bottom
            bottomMargin: Theme.space2
        }

        ListView {
            id: trackList
            anchors.fill: parent
            visible: !view.grouped
            model: controller.tracks
            clip: true
            reuseItems: true
            spacing: 2

            delegate: TrackRow {
                width: ListView.view.width
                indexColumnWidth: view.indexColumnWidth
                artSize: view.artSize
                albumColumnWidth: view.albumColumnWidth
                timeColumnWidth: view.timeColumnWidth
                gutter: view.gutter
            }

            ScrollBar.vertical: ListScroll {}
        }

        ListView {
            id: groupList
            anchors.fill: parent
            visible: view.grouped
            model: controller.groups
            clip: true
            reuseItems: true
            spacing: 2

            delegate: GroupRow {
                width: ListView.view.width
                artSize: view.groupArtSize
                timeColumnWidth: view.timeColumnWidth
                gutter: view.gutter
            }

            ScrollBar.vertical: ListScroll {}
        }
    }

    EmptyState {
        anchors.centerIn: listArea
        visible: view.empty
        glyph: view.searching ? "search" : "note"
        title: view.searching ? Tr.t("library_empty_search_title") : Tr.t("library_empty_title")
        message: view.emptyMessage()
    }
}
