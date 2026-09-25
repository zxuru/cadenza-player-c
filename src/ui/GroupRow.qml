import QtQuick
import Cadenza

/// One row of a grouped library: an artist, an album or a genre.
///
/// The artwork of one of the group's tracks stands for the whole group -- the
/// only picture a group has, since the files carry no artist portraits -- and
/// the line under the name says how much is in there. Opening it narrows the
/// list to that group's tracks.
Rectangle {
    id: row

    // ListView injects the roles; they have to be declared, because a delegate
    // that opts into required properties no longer sees them as context
    // properties.
    required property string key
    required property string name
    required property string subtitle
    required property string coverPath
    required property real duration

    property int artSize: 56
    property int timeColumnWidth: 56
    property int gutter: Theme.space3

    implicitHeight: 68
    height: implicitHeight
    radius: Theme.radiusThumb
    color: pointer.containsMouse ? Theme.surfaceHover : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.duration } }

    Rectangle {
        id: artFrame
        anchors {
            left: parent.left
            leftMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
        width: row.artSize
        height: row.artSize
        radius: Theme.radiusThumb
        color: Theme.surfaceActive
        clip: true

        IconButton {
            anchors.centerIn: parent
            size: 24
            iconSize: 22
            glyph: "note"
            interactive: false
            color: Theme.textFaint
            visible: cover.status !== Image.Ready
        }

        Image {
            id: cover
            anchors.fill: parent
            source: controller.coverUrl(row.coverPath, row.artSize * 2, row.artSize * 2, 0,
                                        Theme.radiusThumb * 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            smooth: true
            visible: status === Image.Ready
        }
    }

    Column {
        anchors {
            left: artFrame.right
            leftMargin: row.gutter
            right: timeLabel.left
            rightMargin: row.gutter
            verticalCenter: parent.verticalCenter
        }
        spacing: 1

        Text {
            width: parent.width
            text: row.name
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontRow
            color: Theme.text
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

    Text {
        id: timeLabel
        anchors {
            right: parent.right
            rightMargin: Theme.space2
            verticalCenter: parent.verticalCenter
        }
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
        onClicked: controller.openGroup(row.key, row.name)
    }
}
