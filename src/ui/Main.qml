import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Cadenza

/// The window. It carries no decoration from the platform on any system: the
/// application draws the whole surface itself -- a rounded pane of lit glass
/// with the panes floating inside it -- and everything a title bar would have
/// owned: the band across the top that drags it, the controls at the right end
/// of the top row, and the edges that resize it.
ApplicationWindow {
    id: window

    /// 0 shows the library, 1 the now playing view, 2 the search for music to
    /// download, 3 the settings.
    property int view: 0

    readonly property bool maximized: visibility === Window.Maximized
    /// Nothing is rounded when the window fills the screen: the desktop's own
    /// corners are the ones on screen then.
    readonly property int rounding: maximized ? 0 : Theme.radiusWindow
    /// Distance between the window's edge and the panes inside it.
    readonly property int gutter: Theme.windowGutter

    title: "Cadenza"
    // The surface is the pane drawn below, so the window itself is empty and
    // what is outside the pane stays transparent.
    color: "transparent"
    background: null
    flags: Qt.Window | Qt.FramelessWindowHint
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    visible: true

    /// The folder dialog hands back a URL; the library is indexed by path.
    function localPath(url)
    {
        var text = String(url)
        if (text.startsWith("file://"))
            text = text.substring(7)
        else if (text.startsWith("file:"))
            text = text.substring(5)
        // "file:///C:/Music" keeps a leading slash that Windows does not want.
        if (text.length > 2 && text.charAt(0) === "/" && text.charAt(2) === ":")
            text = text.substring(1)
        return decodeURIComponent(text)
    }

    function folderUrl(path)
    {
        if (path.length === 0)
            return ""
        return "file://" + (path.charAt(0) === "/" ? "" : "/") + path
    }

    function scanFolder(url)
    {
        var path = window.localPath(url)
        if (path.length > 0)
            controller.scan(path)
    }

    FolderDialog {
        id: folderDialog
        title: Tr.t("library_choose_folder")
        currentFolder: window.folderUrl(controller.rootFolder)
        onAccepted: window.scanFolder(selectedFolder)
    }

    // The pane, and the frame everything else is placed within. It fills the
    // window: an inset would leave the desktop showing around the application,
    // which reads as a second border rather than as depth.
    Item {
        id: pane
        anchors.fill: parent
        // The backdrop is rendered a little larger than the pane so its
        // quantised size always covers it; this keeps the overshoot invisible.
        clip: true

        Backdrop {
            id: backdrop
            anchors.fill: parent
            rounding: window.rounding
        }

        // Dragging the window. It sits under every pane, so only the band that
        // no control covers answers to it: the top edge of the window, the
        // application's own name, and the quiet parts of the top row.
        MouseArea {
            id: dragBand
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: Theme.dragBandHeight
            acceptedButtons: Qt.LeftButton
            onPressed: window.startSystemMove()
            onDoubleClicked: chrome.toggleMaximize()
        }

        Sidebar {
            id: sidebar
            anchors {
                left: parent.left
                leftMargin: window.gutter
                top: parent.top
                topMargin: window.gutter
                bottom: transport.top
                bottomMargin: window.gutter
            }
            view: window.view
            onViewRequested: (target) => window.view = target
        }

        Item {
            id: content
            anchors {
                left: sidebar.right
                leftMargin: window.gutter
                right: parent.right
                rightMargin: window.gutter
                top: parent.top
                topMargin: window.gutter
                bottom: transport.top
                bottomMargin: window.gutter
            }

            StackLayout {
                anchors.fill: parent
                currentIndex: window.view

                LibraryView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                NowPlaying {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                GetMusic {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                Settings {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onFolderRequested: folderDialog.open()
                }
            }

            // The window's controls, at the right end of the row the views
            // start with. They are the reason that row reserves
            // `Theme.windowControlsWidth`.
            TitleBar {
                id: chrome
                targetWindow: window
                anchors {
                    right: parent.right
                    top: parent.top
                    topMargin: Theme.topRowMargin + (Theme.topRowHeight - height) / 2
                }
            }
        }

        TransportBar {
            id: transport
            anchors {
                left: parent.left
                leftMargin: window.gutter
                right: parent.right
                rightMargin: window.gutter
                bottom: parent.bottom
                bottomMargin: window.gutter
            }
            // The artwork in the transport is the way into the hero view: the
            // rail has no row for it, because this is where the track that
            // would be shown there already is.
            onExpandRequested: window.view = 1
        }

        // Slim indeterminate strip along the very top edge while the walk runs.
        // Inset by the rounding, or its ends would sit outside the window's
        // corners.
        Rectangle {
            id: scanStrip
            anchors {
                left: parent.left
                leftMargin: window.rounding + 2
                right: parent.right
                rightMargin: window.rounding + 2
                top: parent.top
            }
            height: 2
            color: Theme.alpha(Theme.accent, 0.18)
            visible: controller.scanning
            z: 20

            Rectangle {
                id: scanRunner
                width: Math.max(48, scanStrip.width * 0.28)
                height: scanStrip.height
                radius: Theme.radiusPill
                color: Theme.accent

                NumberAnimation on x {
                    running: scanStrip.visible
                    from: -scanRunner.width
                    to: scanStrip.width
                    duration: 1100
                    loops: Animation.Infinite
                    easing.type: Easing.InOutQuad
                }
            }
        }

        // A library that could not be opened leaves nothing to show, so the
        // error covers the window instead of the list.
        Rectangle {
            anchors.fill: parent
            radius: window.rounding
            color: Theme.alpha(Theme.bg, 0.97)
            visible: controller.libraryError.length > 0
            z: 30

            EmptyState {
                anchors.centerIn: parent
                glyph: "folder"
                title: Tr.t("library_unavailable")
                message: controller.libraryError
            }
        }
    }

    // Frameless means no platform border to grab, so the window is gripped
    // directly, just inside its own edge. The corners of the window are
    // transparent, and these are what cover them.
    component Grip: MouseArea {
        property int edges: 0
        acceptedButtons: Qt.LeftButton
        enabled: !window.maximized
        onPressed: window.startSystemResize(edges)
    }

    Grip {
        edges: Qt.LeftEdge
        cursorShape: Qt.SizeHorCursor
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: Theme.gripThickness
    }

    Grip {
        edges: Qt.RightEdge
        cursorShape: Qt.SizeHorCursor
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: Theme.gripThickness
    }

    Grip {
        edges: Qt.TopEdge
        cursorShape: Qt.SizeVerCursor
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.gripThickness
    }

    Grip {
        edges: Qt.BottomEdge
        cursorShape: Qt.SizeVerCursor
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.gripThickness
    }

    Grip {
        edges: Qt.LeftEdge | Qt.TopEdge
        cursorShape: Qt.SizeFDiagCursor
        anchors { left: parent.left; top: parent.top }
        width: Theme.gripCorner
        height: Theme.gripCorner
    }

    Grip {
        edges: Qt.RightEdge | Qt.TopEdge
        cursorShape: Qt.SizeBDiagCursor
        anchors { right: parent.right; top: parent.top }
        width: Theme.gripCorner
        height: Theme.gripCorner
    }

    Grip {
        edges: Qt.LeftEdge | Qt.BottomEdge
        cursorShape: Qt.SizeBDiagCursor
        anchors { left: parent.left; bottom: parent.bottom }
        width: Theme.gripCorner
        height: Theme.gripCorner
    }

    Grip {
        edges: Qt.RightEdge | Qt.BottomEdge
        cursorShape: Qt.SizeFDiagCursor
        anchors { right: parent.right; bottom: parent.bottom }
        width: Theme.gripCorner
        height: Theme.gripCorner
    }
}
