import QtQuick
import Cadenza

/// One library row: index or playing indicator, artwork, title with subtitle,
/// album and duration. The column widths are settable so the list header can
/// line up with the rows it labels.
Rectangle {
    id: row

    // ListView injects `index`; it has to be declared, because a delegate that
    // opts into required properties no longer sees it as a context property.
    required property int index
    required property string path
    required property string displayTitle
    required property string subtitle
    required property string album
    required property real duration
    required property int trackNo

    property int indexColumnWidth: 28
    property int artSize: 44
    property int albumColumnWidth: 180
    property int timeColumnWidth: 56
    property int gutter: Theme.space3

    readonly property bool playing: controller.currentRow === index
    readonly property int number: trackNo > 0 ? trackNo : index + 1

    implicitHeight: 56
    height: implicitHeight
    radius: Theme.radiusThumb
    color: playing ? Theme.accentSoft
                   : (pointer.containsMouse ? Theme.surfaceHover : "transparent")
    Behavior on color { ColorAnimation { duration: Theme.duration } }

    Item {
        id: indexSlot
        anchors { left: parent.left; leftMargin: Theme.space2; verticalCenter: parent.verticalCenter }
        width: row.indexColumnWidth
        height: 20

        Text {
            anchors.fill: parent
            visible: !row.playing
            text: row.number
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textFaint
        }

        Row {
            id: bars
            anchors.centerIn: parent
            spacing: 2
            height: 16
            visible: row.playing
            readonly property int barWidth: 2
            width: 3 * barWidth + 2 * spacing

            Repeater {
                model: 3

                Rectangle {
                    id: bar
                    readonly property int phase: index
                    y: (bars.height - height) / 2
                    width: bars.barWidth
                    radius: Theme.radiusPill
                    color: Theme.accent

                    SequentialAnimation on height {
                        running: row.playing
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: 5
                            to: 14
                            duration: 340 + bar.phase * 110
                            easing.type: Easing.InOutQuad
                        }
                        NumberAnimation {
                            from: 14
                            to: 5
                            duration: 340 + bar.phase * 110
                            easing.type: Easing.InOutQuad
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: artFrame
        anchors { left: indexSlot.right; leftMargin: row.gutter; verticalCenter: parent.verticalCenter }
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
            source: controller.coverUrl(row.path, row.artSize * 2, row.artSize * 2, 0,
                                        Theme.radiusThumb * 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            visible: status === Image.Ready
        }
    }

    Item {
        anchors {
            left: artFrame.right
            leftMargin: row.gutter
            right: albumLabel.left
            rightMargin: row.gutter
            top: parent.top
            bottom: parent.bottom
        }

        Column {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: implicitHeight
            spacing: 1

            Text {
                width: parent.width
                text: row.displayTitle
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontRow
                color: row.playing ? Theme.accent : Theme.text
            }

            Text {
                width: parent.width
                text: row.subtitle
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textDim
            }
        }
    }

    Text {
        id: albumLabel
        anchors { right: timeLabel.left; rightMargin: row.gutter; verticalCenter: parent.verticalCenter }
        width: row.albumColumnWidth
        text: row.album
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: Theme.textDim
    }

    Text {
        id: timeLabel
        anchors { right: parent.right; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
        width: row.timeColumnWidth
        horizontalAlignment: Text.AlignRight
        text: controller.formatTime(row.duration)
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: Theme.textFaint
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: controller.playRow(index)
    }
}
