import QtQuick
import QtQuick.Controls
import Cadenza

/// What the library list is a list of.
///
/// A pill naming the browse mode in force -- "Songs", "Artists", "Albums",
/// "Genres" -- that opens the list of them. It stands before the search field
/// it belongs to, so the two read as one sentence: this is what the list below
/// shows, and what the box looks through.
///
/// The list is drawn by hand rather than with a styled `Menu`: the pane
/// material is the application's own, and a control style would bring its own
/// colours and its own paddings into the window.
Item {
    id: browse

    /// The mode in force, out of `controller.browseModes`.
    property string mode: "tracks"
    /// Set while the list of fields is showing.
    readonly property alias open: menu.opened

    /// Shows the list, or puts it away when it is already showing: what the
    /// pill does. A popup's `opened` is what it reports rather than something it
    /// takes, so these are the calls the way in.
    function toggle()
    {
        menu.opened ? menu.close() : menu.open()
    }

    /// Puts the list away, for a view closing it from outside: Escape over the
    /// search field is the press outside this control.
    function close()
    {
        menu.close()
    }

    /// The mode the listener picked out of the list.
    signal selected(string mode)

    readonly property bool hovered: pointer.containsMouse

    /// Height of one row of the list, and the gap under it.
    readonly property int rowHeight: 32
    readonly property int rowSpacing: 2

    /// How wide one row is: the widest field name plus room for the tick.
    ///
    /// Measured off the rows themselves, because a `Repeater`'s items are
    /// reached by index and no binding can watch that: each row reports itself
    /// as it is measured, and the widest one is what the pane is cut to.
    property real rowWidth: 0

    function measureRows()
    {
        let widest = 0
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.itemAt(i)
            if (row)
                widest = Math.max(widest, row.implicitWidth)
        }
        browse.rowWidth = widest
    }

    implicitWidth: pill.width
    implicitHeight: pill.height

    Glass {
        id: pill

        anchors.verticalCenter: parent.verticalCenter
        width: pillRow.width + Theme.space3 * 2
        height: 36
        paneRadius: Theme.radius
        // The one pane in the row that is a control rather than a field, so it
        // lights up on hover the way a button does.
        border.color: browse.open || browse.hovered ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.duration } }

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: Theme.space1

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("library_browse_" + browse.mode)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                font.letterSpacing: 1.2
                color: browse.open ? Theme.accent : Theme.textDim
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                size: 12
                iconSize: 12
                glyph: "chevron"
                interactive: false
                color: browse.open ? Theme.accent : Theme.textFaint
            }
        }

        MouseArea {
            id: pointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // A click rather than a press: while the list is showing it takes
            // every press that lands outside it, this pill included, so a press
            // here is how the list is put away. What reaches this handler is a
            // click on a list that is already away.
            onClicked: browse.toggle()
        }
    }

    Popup {
        id: menu

        // Anchored to the control, so the component can be placed anywhere.
        parent: browse
        // The press that puts the list away is taken by the list itself, so the
        // control underneath does not read the same click as a second press and
        // put the list back up.
        modal: true
        x: 0
        y: pill.height + Theme.space1
        padding: Theme.space1
        background: Glass { paneRadius: Theme.radiusPanel }

        // The pane is sized here rather than left to the popup: a popup asks its
        // content for an implicit size when it opens, which is before the rows
        // exist and, for a positioner, before it has laid anything out.
        width: browse.rowWidth + menu.padding * 2
        height: rows.count * (browse.rowHeight + browse.rowSpacing) - browse.rowSpacing
                + menu.padding * 2

        contentItem: Item {
            Repeater {
                id: rows
                model: controller.browseModes
                onCountChanged: browse.measureRows()

                delegate: Item {
                    id: entry

                    required property string modelData
                    required property int index

                    readonly property bool current: entry.modelData === browse.mode

                    // One width for every row, so the list reads as a pane
                    // rather than as six lengths of text.
                    y: entry.index * (browse.rowHeight + browse.rowSpacing)
                    width: browse.rowWidth
                    height: browse.rowHeight
                    implicitWidth: entryLabel.implicitWidth + Theme.space2 * 3 + checkMark.width

                    onImplicitWidthChanged: browse.measureRows()
                    Component.onCompleted: browse.measureRows()

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: entryHover.containsMouse ? Theme.surfaceHover : "transparent"
                    }

                    Text {
                        id: entryLabel
                        anchors {
                            left: parent.left
                            leftMargin: Theme.space2
                            verticalCenter: parent.verticalCenter
                        }
                        text: Tr.t("library_browse_" + entry.modelData)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        color: entry.current ? Theme.accent : Theme.text
                    }

                    IconButton {
                        id: checkMark
                        anchors {
                            right: parent.right
                            rightMargin: Theme.space2
                            verticalCenter: parent.verticalCenter
                        }
                        size: 16
                        iconSize: 14
                        glyph: "check"
                        interactive: false
                        color: Theme.accent
                        visible: entry.current
                    }

                    MouseArea {
                        id: entryHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            browse.selected(entry.modelData)
                            menu.close()
                        }
                    }
                }
            }
        }
    }
}
