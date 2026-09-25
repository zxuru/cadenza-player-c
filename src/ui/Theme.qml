pragma Singleton

import QtQuick

/// The single source of truth for the interface: every colour, radius, spacing
/// step and type size lives here, so no component ever hardcodes one.
///
/// The palette is one material seen in four lights. `bg` is the void the
/// window sits on, `surface` is the smoked glass every pane is made of, and
/// `accent` is the violet the interface answers with. Nothing else is invented
/// here: the rest of the colour on screen comes from the record being played,
/// which the backdrop blurs behind the glass.
QtObject {
    // -- Palette ----------------------------------------------------------

    /// What is behind the window: the desktop's own colour shows through the
    /// corners, so this only fills the wash when there is no cover to show.
    readonly property color bg: "#0A0812"
    /// Far stop of the wash that stands in for the cover art when a track
    /// carries none. Violet rather than grey, so the glass is always over
    /// something.
    readonly property color bgLift: "#1D1438"

    /// The pane material: dark glass, translucent, so the blurred cover reads
    /// through it. Every pane in the app is filled with this.
    readonly property color surface: Qt.rgba(0.043, 0.035, 0.075, 0.62)
    readonly property color surfaceHover: Qt.rgba(1, 1, 1, 0.07)
    readonly property color surfaceActive: Qt.rgba(1, 1, 1, 0.13)
    readonly property color border: Qt.rgba(1, 1, 1, 0.10)
    /// The light a pane of glass catches along its top edge.
    readonly property color sheen: Qt.rgba(1, 1, 1, 0.10)

    readonly property color text: "#F4F1FB"
    readonly property color textDim: "#AFA7CA"
    readonly property color textFaint: "#8B83A8"

    /// Violet: what the interface uses to say "here" and "playing".
    readonly property color accent: "#A87BFF"
    readonly property color accentDim: "#7C4FE0"
    /// Text and glyphs drawn on top of a solid `accent` fill.
    readonly property color accentText: "#150A26"
    /// The accent as a veil: selection behind text, never behind a glyph.
    readonly property color accentSoft: Qt.rgba(0.659, 0.482, 1.0, 0.16)

    readonly property color danger: "#FF6B7E"
    /// Glyphs drawn on top of a solid `danger` fill.
    readonly property color dangerText: "#260409"

    /// The veil that keeps text legible over a bright cover.
    readonly property color scrim: Qt.rgba(0.039, 0.031, 0.071, 0.55)

    // -- Geometry ---------------------------------------------------------

    /// Rounding of the window itself. The backdrop picture is cut to the same
    /// radius by the image provider, so the two cannot drift apart.
    readonly property int radiusWindow: 14
    /// Rounding of a pane floating inside the window.
    readonly property int radiusPanel: 12

    /// Gap between the window's own edge and the panes floating inside it.
    readonly property int windowGutter: 10
    /// How far inside the window's edge the grips that resize it reach. There
    /// is no decorated border to grab, so the edges are gripped directly; the
    /// window's own corners are transparent, and this is what covers them.
    readonly property int gripThickness: 6
    readonly property int gripCorner: 16

    /// The first row of every view: the search field in the library, the lyrics
    /// toggle in the hero, and the window controls at the right end of both.
    /// Shared, so the three line up instead of being guessed at.
    readonly property int topRowMargin: space4
    readonly property int topRowHeight: 36
    /// Room the window controls take at that right end: three buttons, the
    /// padding of the capsule behind them, and a gap before the content.
    readonly property int windowControlsWidth: 3 * 30 + 2 * space1 + space2 * 2

    /// The band across the top of the window that drags it. It draws nothing:
    /// the window has no title bar, only the part of its edge that moves it.
    readonly property int dragBandHeight: topRowMargin + topRowHeight

    // -- Radii ------------------------------------------------------------
    // The two base steps are joined by the artwork and pill radii, so a corner
    // is still never written down inside a component.
    readonly property int radiusPill: 999
    readonly property int radiusLarge: 16
    readonly property int radius: 10
    readonly property int radiusThumb: 8
    readonly property int radiusSmall: 6

    // -- Spacing scale ----------------------------------------------------
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32

    // -- Type scale -------------------------------------------------------
    readonly property int fontTiny: 11
    readonly property int fontSmall: 12
    readonly property int fontBody: 13
    readonly property int fontRow: 15
    readonly property int fontTitle: 22
    readonly property int fontHero: 34

    /// The platform's own UI family: pinning a different one is a change to
    /// this line alone, and every component follows it.
    readonly property string fontFamily: Qt.application.font.family

    /// Length of the small state changes: hover fills and fades.
    readonly property int duration: 140

    /// `color` with its alpha scaled by `a`, for veils and dimmed fills.
    function alpha(color, a)
    {
        return Qt.rgba(color.r, color.g, color.b, color.a * a)
    }

    /// A slightly lighter `color`, for the raised state of a flat surface.
    function elevate(color)
    {
        return Qt.lighter(color, 1.35)
    }
}
