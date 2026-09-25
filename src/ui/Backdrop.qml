import QtQuick
import Cadenza

/// The window's own pane, and the only surface in the interface that is not
/// glass: the blurred cover of the track that is playing, over a violet wash
/// that stands in when nothing is.
///
/// The picture is rounded by the image provider rather than masked here. Two
/// reasons: a mask is a shader, and on a backend where shaders do not run the
/// whole backdrop would disappear; and rounding the provider's output is one
/// operation on an image that is already being blurred and cached.
Item {
    id: backdrop

    /// Rounding of the window. The provider cuts the picture to the same
    /// value, so a resize cannot leave the two out of step.
    property int rounding: Theme.radiusWindow

    // The size the picture is rendered for. Quantised, so dragging a window
    // edge does not ask for a new decode on every pixel, and rounded up, so
    // the picture always covers the pane it is drawn into.
    readonly property int coverWidth: Math.ceil(width / 32) * 32
    readonly property int coverHeight: Math.ceil(height / 32) * 32

    // Clips the picture's overflow past its quantised box.
    clip: true

    // The wash: what the glass sits over when there is no cover, and what
    // shows through the picture's softened edges when there is one.
    Rectangle {
        anchors.fill: parent
        radius: backdrop.rounding
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.bgLift }
            GradientStop { position: 1.0; color: Theme.bg }
        }
    }

    Image {
        width: backdrop.coverWidth
        height: backdrop.coverHeight
        source: controller.coverUrl(controller.player.path, width, height, 34, backdrop.rounding)
        sourceSize: Qt.size(width, height)
        fillMode: Image.Pad
        asynchronous: true
        cache: true
        opacity: 0.62
    }

    // The scrim. A cover can be as bright as it likes; the list still has to
    // be readable over it.
    Rectangle {
        anchors.fill: parent
        radius: backdrop.rounding
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.alpha(Theme.scrim, 0.75) }
            GradientStop { position: 1.0; color: Theme.alpha(Theme.scrim, 1.15) }
        }
    }

    // The window's edge: the only line the application draws around itself.
    Rectangle {
        anchors.fill: parent
        radius: backdrop.rounding
        color: "transparent"
        border.width: 1
        border.color: Theme.border
    }
}
