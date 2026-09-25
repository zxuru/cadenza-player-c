import QtQuick
import QtQuick.Controls
import Cadenza

/// Everything that is set once and then left alone: the music folder and what
/// the index holds of it, the lyrics, and the language. These used to live at
/// the foot of the navigation rail, where they were read far more often than
/// they were changed and had only the width of the rail to be changed in.
Item {
    id: view

    /// Asks for the folder picker. The window owns it: it is a platform
    /// dialog, and the window is what knows the folder the library is in.
    signal folderRequested()

    /// The column the settings are laid out in. Bounded, so a wide window
    /// grows the empty space beside them instead of stretching a label away
    /// from the control it belongs to.
    readonly property int contentWidth: Math.min(width - Theme.space4 * 3, 760)

    /// A pane of glass holding the controls of one subject, as the rail and
    /// the transport hold theirs.
    component Card: Glass {
        id: card

        default property alias rows: body.data

        height: body.height + Theme.space4 * 2

        Column {
            id: body
            anchors {
                left: parent.left
                leftMargin: Theme.space4
                right: parent.right
                rightMargin: Theme.space4
                top: parent.top
                topMargin: Theme.space4
            }
            spacing: Theme.space3
        }
    }

    /// One setting: what it is and what it means on the left, the control that
    /// changes it on the right, on the line the two of them share.
    component SettingRow: Item {
        id: row

        property string title: ""
        /// A sentence under the title, for the settings whose name does not
        /// say what they do. An empty string leaves the line out.
        property string hint: ""
        default property alias control: controlSlot.data

        width: parent ? parent.width : 0
        height: Math.max(labelBlock.implicitHeight, controlSlot.height)

        Column {
            id: labelBlock
            anchors {
                left: parent.left
                right: controlSlot.left
                rightMargin: Theme.space4
                verticalCenter: parent.verticalCenter
            }
            spacing: 2

            Text {
                width: parent.width
                text: row.title
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.text
            }

            Text {
                width: parent.width
                text: row.hint
                visible: text.length > 0
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textDim
            }
        }

        Item {
            id: controlSlot
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: childrenRect.width
            height: childrenRect.height
        }
    }

    /// One of the two things that can be done to the folder. A pill, like
    /// every other control in the window, with its name written on it: in here
    /// there is room for words, so no glyph has to stand alone.
    component ActionButton: Rectangle {
        id: action

        property string label: ""
        property string glyph: ""

        signal clicked()

        width: (action.glyph.length > 0 ? actionGlyph.width + Theme.space1 : 0)
               + actionLabel.implicitWidth + Theme.space3 * 2
        height: 32
        radius: Theme.radiusPill
        color: actionPointer.containsMouse ? Theme.accentSoft : "transparent"
        border.width: 1
        border.color: actionPointer.containsMouse ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.duration } }

        IconButton {
            id: actionGlyph
            anchors {
                left: parent.left
                leftMargin: Theme.space3
                verticalCenter: parent.verticalCenter
            }
            size: 16
            iconSize: 16
            glyph: action.glyph
            interactive: false
            color: actionPointer.containsMouse ? Theme.accent : Theme.textDim
            visible: action.glyph.length > 0
        }

        Text {
            id: actionLabel
            anchors {
                left: action.glyph.length > 0 ? actionGlyph.right : parent.left
                leftMargin: action.glyph.length > 0 ? Theme.space1 : Theme.space3
                verticalCenter: parent.verticalCenter
            }
            text: action.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.text
        }

        MouseArea {
            id: actionPointer
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.clicked()
        }
    }

    /// Small uppercase label above a card, the one the rail uses for its own
    /// sections.
    component SectionLabel: Text {
        property string title: ""
        text: title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        font.letterSpacing: 1.4
        color: Theme.textFaint
    }

    Text {
        id: pageTitle
        anchors {
            left: parent.left
            leftMargin: Theme.space4
            top: parent.top
            topMargin: Theme.topRowMargin + (Theme.topRowHeight - height) / 2
        }
        text: Tr.t("nav_settings")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontRow
        font.bold: true
        color: Theme.text
    }

    Flickable {
        id: scroll
        anchors {
            left: parent.left
            leftMargin: Theme.space4
            right: parent.right
            rightMargin: Theme.space4
            top: pageTitle.bottom
            topMargin: Theme.space4
            bottom: parent.bottom
            bottomMargin: Theme.space2
        }
        contentWidth: width
        contentHeight: sections.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            id: sectionScrollBar
            policy: ScrollBar.AsNeeded
            implicitWidth: 8

            background: Rectangle { implicitWidth: 8; color: "transparent" }

            contentItem: Rectangle {
                implicitWidth: 6
                radius: Theme.radiusPill
                color: sectionScrollBar.pressed ? Theme.accentDim
                                                : Theme.alpha(Theme.textFaint, 0.7)
            }
        }

        Column {
            id: sections
            width: view.contentWidth
            spacing: Theme.space5

            Column {
                width: parent.width
                spacing: Theme.space2

                SectionLabel { title: Tr.t("settings_music_folder") }

                Card {
                    width: sections.width

                    // Where the library is, and what is in it.
                    Item {
                        width: parent.width
                        height: Math.max(folderMark.height, rootPath.height)

                        IconButton {
                            id: folderMark
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            size: 20
                            iconSize: 18
                            glyph: "folder"
                            interactive: false
                            color: Theme.textFaint
                        }

                        Text {
                            id: rootPath
                            anchors {
                                left: folderMark.right
                                leftMargin: Theme.space2
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                            }
                            text: controller.rootFolder
                            elide: Text.ElideMiddle
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.text
                        }
                    }

                    // What can be done to it, and how much of it there is. The
                    // count is here rather than under the playlists: it is the
                    // index's own number, not the rail's.
                    Item {
                        width: parent.width
                        height: Math.max(chooseButton.height, trackCount.height)

                        ActionButton {
                            id: chooseButton
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            label: Tr.t("settings_choose_folder")
                            glyph: "folder"
                            onClicked: view.folderRequested()
                        }

                        ActionButton {
                            anchors {
                                left: chooseButton.right
                                leftMargin: Theme.space2
                                verticalCenter: parent.verticalCenter
                            }
                            label: Tr.t("settings_rescan")
                            glyph: "refresh"
                            onClicked: controller.rescan()
                        }

                        Text {
                            id: trackCount
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            text: Tr.n("library_tracks", controller.trackCount)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textFaint
                        }
                    }

                    // What the walk and the downloads are doing, which the
                    // rail used to say. It is one line about the index, so it
                    // lives with the folder it describes.
                    Text {
                        width: parent.width
                        text: controller.status
                        visible: text.length > 0
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textFaint
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.space2

                SectionLabel { title: Tr.t("settings_lyrics_section") }

                Card {
                    width: sections.width

                    SettingRow {
                        title: Tr.t("settings_lyrics_show")
                        hint: Tr.t("settings_lyrics_show_hint")

                        TogglePill {
                            glyph: "lyrics"
                            label: on ? Tr.t("toggle_on") : Tr.t("toggle_off")
                            on: controller.lyricsVisible
                            onToggled: controller.lyricsVisible = !controller.lyricsVisible
                        }
                    }

                    SettingRow {
                        title: Tr.t("settings_lyrics_online")
                        hint: Tr.t("settings_lyrics_online_hint")

                        TogglePill {
                            label: on ? Tr.t("toggle_on") : Tr.t("toggle_off")
                            on: controller.onlineLyrics
                            onToggled: controller.onlineLyrics = !controller.onlineLyrics
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.space2

                SectionLabel { title: Tr.t("language_section") }

                Card {
                    width: sections.width

                    // One row per locale file, and the system's own language
                    // first: the list is the directory the files are in, so a
                    // language added there appears here without anything to
                    // keep in step.
                    Column {
                        id: languageList
                        width: parent.width
                        spacing: 1

                        Repeater {
                            // The empty code is the system's language, which is
                            // what the interface follows until one of the
                            // others is chosen.
                            model: [""].concat(controller.translator.languages)

                            Rectangle {
                                id: languageRow
                                required property string modelData

                                readonly property bool active: languageRow.modelData.length === 0
                                                               ? controller.translator.followsSystem
                                                               : controller.translator.language === languageRow.modelData

                                width: languageList.width
                                height: 30
                                radius: Theme.radiusSmall
                                color: languageRow.active
                                       ? Theme.accentSoft
                                       : (languagePointer.containsMouse ? Theme.surfaceHover
                                                                        : "transparent")
                                Behavior on color { ColorAnimation { duration: Theme.duration } }

                                Text {
                                    anchors {
                                        left: parent.left
                                        leftMargin: Theme.space3
                                        right: parent.right
                                        rightMargin: Theme.space3
                                        verticalCenter: parent.verticalCenter
                                    }
                                    // A language is named in itself, by its own
                                    // locale file: the list is meant to be
                                    // readable to whoever is looking for their
                                    // own.
                                    text: languageRow.modelData.length === 0
                                          ? Tr.t("language_system")
                                          : controller.translator.languageName(languageRow.modelData)
                                    elide: Text.ElideRight
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.bold: languageRow.active
                                    color: languageRow.active ? Theme.accent : Theme.textDim
                                }

                                MouseArea {
                                    id: languagePointer
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (languageRow.modelData.length === 0)
                                            controller.translator.followSystem()
                                        else
                                            controller.translator.language = languageRow.modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
