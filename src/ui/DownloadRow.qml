import QtQuick
import Cadenza

/// One search result: what the catalogue found, and one pill that is both the
/// action and how far the action has got.
///
/// The pill fills as the bytes land. That is the whole progress meter -- the
/// percentage is the same fact as the fill, so it is written on it -- and it
/// is where the click was going to be anyway. Once the file is in the library
/// the pill says so and nothing has to be read to know it.
Item {
    id: row

    required property int index
    required property string title
    required property string artist
    required property string year
    /// The cover YouTube serves for the result, at the size this row shows it.
    required property string thumbnail
    required property bool working
    required property bool waiting
    required property bool done
    required property bool failed
    required property real progress
    /// One line about the download, as a key and the values its placeholders
    /// are filled from: rendered here so it follows the language.
    required property string detailKey
    required property var detailValues

    readonly property string detail: detailKey.length > 0
                                     ? Tr.t(detailKey, detailValues)
                                     : ""

    readonly property bool busy: working || waiting

    /// What the row says under the title: what the download is doing, or did,
    /// or -- when there is nothing to report -- who made the recording.
    readonly property string subtitle: detail.length > 0
                                        ? detail
                                        : [artist, year].filter(part => part.length > 0).join(" \u00b7 ")

    /// What the pill says. Hovering asks what the click would do instead --
    /// which, while a download is running, is to stop it.
    function pillLabel()
    {
        if (waiting)
            return pointer.containsMouse ? Tr.t("download_cancel") : Tr.t("download_waiting")
        if (working)
            return pointer.containsMouse ? Tr.t("download_cancel") : Math.round(progress * 100) + "%"
        if (done)
            return pointer.containsMouse ? Tr.t("download_again") : Tr.t("download_in_library")
        if (failed)
            return Tr.t("download_try_again")
        return Tr.t("download_start")
    }

    function activate()
    {
        if (busy)
            controller.cancelDownload(index)
        else
            controller.downloadResult(index)
    }

    /// The cover's size: a row is a line of text beside a picture, and the
    /// picture is what says at a glance which of two results is the one.
    readonly property int artSize: 44
    /// Where the text starts, as a margin from the row's own left edge: past
    /// the cover, or where it always did when the catalogue had no picture to
    /// show. Written against the frame's geometry and not against `parent`,
    /// which is null until the row is in a list and would freeze the binding at
    /// nothing.
    readonly property real textLeft: artFrame.visible ? artFrame.x + artFrame.width + Theme.space3
                                                      : Theme.space4

    implicitHeight: 60
    height: implicitHeight


    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.space2
        radius: Theme.radiusThumb
        color: pointer.containsMouse ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.duration } }
    }

    // The whole row is the target, not only the pill: a result is one line and
    // the click means one thing.
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.activate()
    }

    // The cover, drawn the way the library draws its rows: a frame that holds a
    // quiet placeholder while the picture is still coming, and the picture
    // itself once it is there. The frame clips it, which is what rounds its
    // corners.
    Rectangle {
        id: artFrame
        anchors {
            left: parent.left
            leftMargin: Theme.space3
            verticalCenter: parent.verticalCenter
        }
        visible: row.thumbnail.length > 0
        width: row.artSize
        height: row.artSize
        radius: Theme.radiusThumb
        color: Theme.surfaceActive
        clip: true

        IconButton {
            anchors.centerIn: parent
            size: 20
            iconSize: 18
            glyph: "note"
            interactive: false
            color: Theme.textFaint
            visible: cover.status !== Image.Ready
        }

        Image {
            id: cover
            anchors.fill: parent
            // A URL, not a file: the picture is YouTube's, and Qt fetches it
            // the way it fetches any other image. It is cached by the engine,
            // so the same result twice is one download.
            source: row.thumbnail
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            mipmap: true
            visible: status === Image.Ready
        }
    }

    Column {
        id: text
        anchors {
            left: parent.left
            leftMargin: row.textLeft
            right: pill.left
            rightMargin: Theme.space3
            verticalCenter: parent.verticalCenter
        }
        spacing: 1

        Text {
            width: parent.width
            text: row.title
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontRow
            color: row.done ? Theme.accent : Theme.text
        }

        Text {
            width: parent.width
            text: row.subtitle
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: row.failed ? Theme.danger : Theme.textDim
        }
    }

    // The pill: frame, fill, and what it says. Declared after the row's own
    // mouse area so a click on it stops there.
    Item {
        id: pill
        anchors {
            right: parent.right
            rightMargin: Theme.space4
            verticalCenter: parent.verticalCenter
        }
        width: 120
        height: 28

        Rectangle {
            id: pillFrame
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: pillPointer.containsMouse || row.busy ? Theme.surfaceHover : "transparent"
            border.width: 1
            border.color: row.failed
                          ? Theme.danger
                          : (row.done ? Theme.accent : Theme.border)
            Behavior on color { ColorAnimation { duration: Theme.duration } }
        }

        Rectangle {
            id: pillFill
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: Math.round(parent.width * row.progress)
            radius: pillFrame.radius
            color: row.failed ? Theme.alpha(Theme.danger, 0.5) : Theme.accentSoft
            // Nothing is drawn until something has been asked for: an empty
            // fill is the idle state, and a bar at zero would read as a
            // download that is stuck.
            visible: row.progress > 0
        }

        IconButton {
            id: tick
            anchors {
                right: pillLabel.left
                rightMargin: Theme.space1
                verticalCenter: parent.verticalCenter
            }
            visible: row.done
            size: 16
            iconSize: 14
            glyph: "check"
            interactive: false
            color: Theme.accent
        }

        Text {
            id: pillLabel
            anchors.centerIn: parent
            // Room for the tick beside it, so the pair still reads as centred.
            anchors.horizontalCenterOffset: row.done ? 10 : 0
            text: row.pillLabel()
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: row.done ? Theme.accent
                            : (row.failed ? Theme.danger : (row.busy ? Theme.text : Theme.textDim))
        }

        MouseArea {
            id: pillPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.activate()
        }
    }
}
