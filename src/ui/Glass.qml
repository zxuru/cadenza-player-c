import QtQuick
import Cadenza

/// The one material in the app: a pane of smoked glass floating over the
/// backdrop. Panes are only ever rectangles with one of the two radii, so a
/// surface cannot drift away from the rest.
///
/// The sheen along the top edge is what makes the pane read as glass rather
/// than as a translucent grey box: it is the light the pane catches. It is
/// inset by the border and rounded to the same radius minus one, which puts it
/// exactly one pixel inside the pane's own edge at every corner -- a plain
/// rectangle would poke past the rounding.
Rectangle {
    id: glass

    /// `Theme.radiusWindow` for the window's own pane, `Theme.radiusPanel` for
    /// one floating inside it.
    property int paneRadius: Theme.radiusPanel
    /// Turned off for a pane that is cut off by another one.
    property bool lit: true

    radius: paneRadius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        visible: glass.lit
        radius: Math.max(0, glass.paneRadius - 1)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.sheen }
            GradientStop { position: 0.42; color: "transparent" }
        }
    }
}
