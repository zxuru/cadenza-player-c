import QtQuick
import QtQuick.Window
import Cadenza

/// The window's own controls. There is no title bar for them to sit in, so they
/// float at the right end of the top row, on the same material as the panes --
/// a title bar's job, without a strip of empty space across the top of the
/// window. Dragging lives in `Main.qml`, on the band above this row and on the
/// application's own name.
Item {
    id: bar

    /// The window these controls act on.
    property Window targetWindow

    readonly property bool maximized: targetWindow
        ? targetWindow.visibility === Window.Maximized
        : false

    implicitWidth: capsule.width
    implicitHeight: capsule.height
    width: implicitWidth
    height: implicitHeight

    /// The window manager decides what "maximized" means and reports it back
    /// through `visibility`, which is what the middle button follows.
    function toggleMaximize()
    {
        if (!targetWindow)
            return
        if (bar.maximized)
            targetWindow.showNormal()
        else
            targetWindow.showMaximized()
    }

    Glass {
        id: capsule
        anchors.centerIn: parent
        width: buttons.width + Theme.space2
        height: buttons.height + Theme.space2
        paneRadius: Theme.radiusSmall
    }

    Row {
        id: buttons
        anchors.centerIn: parent
        spacing: Theme.space1

        IconButton {
            size: 30
            iconSize: 16
            glyph: "minimize"
            tooltip: Tr.t("window_minimize")
            onClicked: if (bar.targetWindow)
                           bar.targetWindow.showMinimized()
        }

        IconButton {
            size: 30
            iconSize: 16
            glyph: bar.maximized ? "restore" : "maximize"
            tooltip: bar.maximized ? Tr.t("window_restore") : Tr.t("window_maximize")
            onClicked: bar.toggleMaximize()
        }

        IconButton {
            size: 30
            iconSize: 16
            glyph: "close"
            danger: true
            tooltip: Tr.t("window_close")
            onClicked: if (bar.targetWindow)
                           bar.targetWindow.close()
        }
    }
}
