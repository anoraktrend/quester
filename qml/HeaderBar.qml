import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quester 1.0

ToolBar {
    id: root
    implicitHeight: 50
    property bool transparentBackground: false
    property real fontScale: 1.0
    property string viewState: "libraryView"
    property string viewMode: "flow"
    property bool useProjectM: false

    signal openSettings()
    signal openVisualizerSettings()
    signal requestVisualizer()
    signal requestQueue()
    signal requestLibrary()
    signal requestRefresh()
    signal toggleProjectM()
    signal setViewMode(string mode)
    signal requestBrowser()

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.transparentBackground ? palette.dark : palette.mid}
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    SystemPalette { id: palette }

    property alias title: titleLabel.text

    Action {
        id: fullscreenAction
        text: qsTr("Toggle Fullscreen")
        shortcut: "F11"
        onTriggered: mpdClient.toggleFullscreen()
    }

    Action {
        id: quitAction
        text: qsTr("Quit")
        shortcut: "Ctrl+Q"
        onTriggered: mpdClient.quitApplication()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Image {
            source: "qrc:/Quester.svg"
            sourceSize: Qt.size(32, 32)
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            fillMode: Image.PreserveAspectFit
        }
        Label {
            id: titleLabel
            text: qsTr("Quester")
            color: palette.windowText
            font.bold: true
            font.pixelSize: 16 * root.fontScale
            Layout.fillWidth: true
        }

        ToolButton {
            id: presetMenuButton
            text: qsTr("Colors")
            visible: root.viewState !== "libraryView"
            Layout.preferredWidth: visible ? implicitWidth : 0
            onClicked: root.openVisualizerSettings()
        }

        ToolButton {
            id: sortmenuButton
            icon.source: "image://theme/sort-presence"
            icon.color: palette.windowText
            icon.width: 24 * root.fontScale; icon.height: 24 * root.fontScale
            visible: root.viewState === "libraryView"
            Layout.preferredWidth: visible ? implicitWidth : 0

            onClicked: sortby.open()
    
            Menu {
                id: sortby
                y: parent.height
                title: qsTr("Sort By")

                 MenuItem {
                    text: qsTr("Artist")
                    checkable: true
                    checked: mpdClient.sortMode === MpdClient.AlbumArtist
                    onTriggered: mpdClient.sortMode = MpdClient.AlbumArtist
                }
                MenuItem {
                    text: qsTr("Album")
                    checkable: true
                    checked: mpdClient.sortMode === MpdClient.Album
                    onTriggered: mpdClient.sortMode = MpdClient.Album
                    }
                MenuItem {
                    text: qsTr("Artist & Year")
                    checkable: true
                    checked: mpdClient.sortMode === MpdClient.ArtistYear
                    onTriggered: mpdClient.sortMode = MpdClient.ArtistYear
                }
            }
        }

        ToolButton {
            id: menuButton
            icon.source: "image://theme/application-menu"
            icon.color: palette.windowText
            icon.width: 24 * root.fontScale; icon.height: 24 * root.fontScale
            
            onClicked: appMenu.open()

            Menu {
                id: appMenu
                y: parent.height

                MenuItem {
                    text: qsTr("Visualizer")
                    visible: root.viewState !== "visualizerView" 
                    onClicked: root.requestVisualizer()
                }
                
                MenuItem {
                    text: qsTr("Toggle ProjectM")
                    checkable: true
                    visible: root.viewState === "visualizerView"
                    checked: root.useProjectM
                    onTriggered: root.toggleProjectM()
                }

                MenuItem {
                    text: qsTr("Queue")
                    visible: root.viewState !== "queueView"
                    onClicked: root.requestQueue()
                }

                MenuItem {
                    text: qsTr("Return to Library")
                    visible: root.viewState !== "libraryView"
                    onClicked: root.requestLibrary()
                }

                MenuItem {
                    text: qsTr("Refresh Library")
                    onTriggered: root.requestRefresh()
                }

                MenuSeparator {}

                RadioButton {
                    text: qsTr("Cover Flow")
                    visible: root.viewState === "libraryView"
                    checked: root.viewMode === "flow"
                    onClicked: {
                        root.setViewMode("flow")
                        appMenu.close()
                    }
                }
                RadioButton {
                    text: qsTr("Grid View")
                    visible: root.viewState === "libraryView"
                    checked: root.viewMode === "grid"
                    onClicked: {
                        root.setViewMode("grid")
                        appMenu.close()
                    }
                }
                RadioButton {
                    text: qsTr("Browser")
                    visible: root.viewState === "libraryView"
                    checked: root.viewMode === "browser"
                    onClicked: {
                        root.setViewMode("browser")
                        root.requestBrowser()
                        appMenu.close()
                    }
                }
                MenuSeparator {}

                MenuItem {
                    text: qsTr("Settings")
                    onTriggered: root.openSettings()
                }

                MenuSeparator {}

                MenuItem { action: fullscreenAction }
                MenuItem { action: quitAction }
            }
        }
    }
}