import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls



import QtQuick.Controls.Material 2.15
import Qt.labs.platform 1.1 as Platform

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: qsTr("Quester")
    color: palette.window

    SystemPalette {
        id: palette
        onHighlightChanged: AudioVisualizer.updateSystemColors(highlight, text)
        onTextChanged: AudioVisualizer.updateSystemColors(highlight, text)
    }
    Material.theme: palette.window.hslLightness > 0.5 ? Material.Light : Material.Dark
    Material.accent: palette.highlight

    // A HeaderBar provides Client-Side Decorations, which is the standard on
    // many Wayland desktops like GNOME. It also provides a place for window controls.
    header: HeaderBar {
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


        ToolButton {
            id: presetMenuButton
            text: qsTr("Colors")
            visible: coverFlow.state === "visualizerView"
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: menuButton.left
            anchors.rightMargin: 10
            onClicked: presetMenu.open()

            Menu {
                id: presetMenu
                y: parent.height
                Repeater {
                    model: AudioVisualizer.presetNames
                    RadioButton {
                        text: modelData
                        checked: AudioVisualizer.currentPreset === modelData
                        onClicked: {
                            AudioVisualizer.currentPreset = modelData
                            presetMenu.close()
                        }
                    }
                }
            }
        }

        // Application Menu Button (Hamburger Menu)
        ToolButton {
            id: menuButton
            text: "\u2630" // Hamburger icon
            font.pixelSize: 20
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            onClicked: appMenu.open()

            Menu {
                id: appMenu
                y: parent.height

                MenuItem {
                    text: qsTr("Visualizer")
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
                    onClicked: goToVisualizer()
                }
                
                MenuItem {
                    text: qsTr("Return to Library")
                    visible: coverFlow.state === "visualizerView"
                    height: visible ? implicitHeight : 0
                    onTriggered: coverFlow.state = "libraryView"
                }

                MenuItem {
                    text: qsTr("Refresh Library")
                    onTriggered: mpdClient.refreshLibrary()
                }

                MenuSeparator {
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
                }

                RadioButton {
                    text: qsTr("Cover Flow")
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
                    checked: coverFlow.viewMode === "flow"
                    onClicked: {
                        coverFlow.viewMode = "flow"
                        appMenu.close()
                    }
                }
                RadioButton {
                    text: qsTr("Grid View")
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
                    checked: coverFlow.viewMode === "grid"
                    onClicked: {
                        coverFlow.viewMode = "grid"
                        appMenu.close()
                    }
                }
                RadioButton {
                    text: qsTr("Browser")
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
                    checked: coverFlow.viewMode === "browser"
                    onClicked: {
                        coverFlow.viewMode = "browser"
                        mpdClient.browsePath(mpdClient.currentPath)
                        appMenu.close()
                    }
                }

                MenuSeparator {}

                MenuItem { action: fullscreenAction }
                MenuItem { action: quitAction }
            }
        }
    }
    
    Component.onCompleted: {
        AudioVisualizer.updateSystemColors(palette.highlight, palette.text)
    }

    function goToVisualizer() {
        if (pathView.currentIndex !== mpdClient.currentAlbumIndex && mpdClient.currentAlbumIndex !== -1) {
            pathView.currentIndex = mpdClient.currentAlbumIndex
            visualizerTimer.start()
        } else {
            coverFlow.state = "visualizerView"
        }
    }

    Timer {
        id: visualizerTimer
        interval: 550
        onTriggered: coverFlow.state = "visualizerView"
    }

    Item {
        id: coverFlow
        anchors.fill: parent
        property string viewMode: "flow"
        state: "libraryView"
        
        PathView {
            id: pathView
            // The HeaderBar is part of the ApplicationWindow chrome, so the content area starts at the top.
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            height: 250
            z: 2
            model: mpdClient.albumModel
            pathItemCount: window.visibility === Window.FullScreen ? 13 : 5
            preferredHighlightBegin: 0.5
            preferredHighlightEnd: 0.5
            highlightRangeMode: PathView.StrictlyEnforceRange
            
            currentIndex: mpdClient.currentAlbumIndex

            Behavior on currentIndex {
                NumberAnimation { duration: 500; easing.type: Easing.InOutQuad }
            }

            onCurrentIndexChanged: {
                mpdClient.loadAlbumTracks(currentIndex)
            }

            transform: Scale {
                id: pathViewScale
                origin.x: pathView.width / 2
                origin.y: pathView.height / 2
                xScale: 1.0
                yScale: 1.0
            }

            delegate: Rectangle {
                width: 200
                height: 200
                color: palette.base
                scale: PathView.iconScale !== undefined ? PathView.iconScale : 1.0
                opacity: PathView.iconOpacity !== undefined ? PathView.iconOpacity : 1.0
                z: PathView.z !== undefined ? PathView.z : 0
                radius: 5
                border.width: model.art ? 2 : 0 // Hide border when there's no art
                border.color: Material.accent
                antialiasing: true
                
                Image {
                    anchors.fill: parent
                    anchors.margins: 2
                    source: model.art
                    fillMode: Image.PreserveAspectCrop
                }
                
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 10
                    text: model.name
                    color: palette.text
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    visible: !model.art
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        // When an item is clicked, it becomes the current item.
                        mpdClient.playAlbum(model.artist, model.name) // Play the album
                        pathView.currentIndex = index
                    }
                }
            }

            path: Path {
                startX: 0; startY: 125
                PathAttribute { name: "iconScale"; value: 0.5 }
                PathAttribute { name: "iconOpacity"; value: 0.6 }
                PathAttribute { name: "z"; value: 0 }
                PathLine { x: coverFlow.width * 0.5; y: 125 }
                PathAttribute { name: "iconScale"; value: 1.2 }
                PathAttribute { name: "iconOpacity"; value: 1.0 }
                PathAttribute { name: "z"; value: 100 }
                PathLine { x: coverFlow.width; y: 125 }
                PathAttribute { name: "iconScale"; value: 0.5 }
                PathAttribute { name: "iconOpacity"; value: 0.6 }
                PathAttribute { name: "z"; value: 0 }
            }
        }

        GridView {
            id: albumGridView
            anchors.top: parent.top
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            clip: true
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "grid"
            
            cellWidth: 180
            cellHeight: 220
            model: mpdClient.albumModel
            
            delegate: Item {
                width: albumGridView.cellWidth
                height: albumGridView.cellHeight
                
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 10
                    color: "transparent"
                    
                    Rectangle {
                        id: artContainer
                        height: width
                        width: parent.width
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: palette.base
                        radius: 5
                        border.width: model.art ? 2 : 0
                        border.color: Material.accent
                        
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            source: model.art
                            fillMode: Image.PreserveAspectCrop
                        }
                        
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 10
                            text: model.name
                            color: palette.text
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                            visible: !model.art
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                pathView.currentIndex = index
                                coverFlow.viewMode = "flow"
                            }
                        }
                    }
                    
                    Text {
                        anchors.top: artContainer.bottom
                        anchors.topMargin: 5
                        width: parent.width
                        text: model.name
                        font.bold: true
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: palette.text
                    }
                    
                    Text {
                        anchors.top: artContainer.bottom
                        anchors.topMargin: 22
                        width: parent.width
                        text: model.artist
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    }
                }
            }
            
            ScrollBar.vertical: ScrollBar { }
        }

        ListView {
            id: browserListView
            anchors.top: parent.top
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            clip: true
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "browser"
            model: mpdClient.browserModel
            
            delegate: Item {
                width: ListView.view.width
                height: 50
                
                Rectangle {
                    anchors.fill: parent
                    color: index % 2 == 0 ? palette.base : palette.alternateBase
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (model.isDir) {
                                mpdClient.browsePath(model.path)
                            } else {
                                mpdClient.playTrack(model.path)
                            }
                        }
                    }
                }
                
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: (model.isDir ? "📁 " : "🎵 ") + model.name
                    color: palette.text
                    font.pixelSize: 16
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }

        VisualizerView {
            id: visualizerView
            anchors.fill: parent
            magnitudes: AudioVisualizer.magnitudes
            albumArt: mpdClient.albumArt
            contentBottomMargin: 100
            active: coverFlow.state === "visualizerView" && mpdClient.state === "play"
            z: 10
            onClicked: coverFlow.state = "libraryView"

            onWidthChanged: {
                if (width > 0) {
                    AudioVisualizer.width = width;
                }
            }
            
            Component.onCompleted: {
                if (width > 0) {
                    AudioVisualizer.width = width;
                }
            }

            onActiveChanged: {
                if (active) {
                    AudioVisualizer.start();
                } else {
                    AudioVisualizer.stop();
                }
            }
        }

        Rectangle {
            id: gradientRect
            anchors.left: pathView.left
            anchors.right: pathView.right
            anchors.bottom: pathView.bottom
            height: 250
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: palette.window }
            }
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "flow"
            z: pathView.z - 1
        }

        Item {
            id: bottomControls
            x: 0
            width: parent.width
            height: 100
            y: parent.height - height
            z: 20

            Behavior on y {
                NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }

            Rectangle {
                anchors.fill: parent
                visible: coverFlow.state === "visualizerView"
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: Qt.hsla(Material.hue, 0, 0, 0.8) }
                }
            }

            ProgressBar {
                id: progressBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                from: 0
                to: mpdClient.duration > 0 ? mpdClient.duration : 1
                value: mpdClient.elapsed

                background: Rectangle {
                    color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.12)
                }
            }


            RowLayout {
                anchors.top: progressBar.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 20

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: mpdClient.title
                        font.pixelSize: 18
                        font.bold: true
                        color: palette.windowText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: mpdClient.artist + " - " + mpdClient.album
                        font.pixelSize: 14
                        color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.7)
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    spacing: 15
                    Button { text: "◀◀"; onClicked: mpdClient.previous(); flat: true }
                    Button {
                        id: playPauseButton
                        text: mpdClient.state === "play" ? "❚❚" : "▶"
                        onClicked: mpdClient.togglePlayPause()
                        font.pixelSize: 20
                        width: 40; height: 40
                    }
                    Button { text: "▶▶"; onClicked: mpdClient.next(); flat: true }
                }
            }
        }

        ListView {
            id: trackListView
            x: 0
            width: parent.width
            y: coverFlow.state === "libraryView" ? (pathView.y + pathView.height) : (bottomControls.y + bottomControls.height)
            height: coverFlow.state === "libraryView" ? (bottomControls.y - y) : (coverFlow.height - y)
            Behavior on y {
                NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }
            Behavior on height {
                NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }

            model: mpdClient.trackModel
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "flow"
            clip: true
            
            delegate: Item {
                width: ListView.view.width
                height: 40
                
                Rectangle {
                    anchors.fill: parent
                    color: index % 2 == 0 ? palette.base : palette.alternateBase
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: mpdClient.playTrack(model.uri)
                    }
                }
                
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.title
                    color: palette.text
                    font.pixelSize: 14
                }
                
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.duration
                    color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.7)
                    font.pixelSize: 12
                }
            }
        }

        states: [
            State {
                name: "libraryView"
                PropertyChanges { target: pathView; opacity: 1.0; visible: coverFlow.viewMode === "flow" }
                PropertyChanges { target: pathViewScale; xScale: 1.0; yScale: 1.0 }
                PropertyChanges { target: visualizerView; opacity: 0.0; visible: false }
                PropertyChanges { target: gradientRect; opacity: 1.0 }
            },
            State {
                name: "visualizerView"
                PropertyChanges { target: pathView; opacity: 0.0; visible: false }
                PropertyChanges { target: pathViewScale; xScale: 5.0; yScale: 5.0 }
                PropertyChanges { target: visualizerView; opacity: 1.0; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
            }
        ]

        transitions: [
            Transition {
                from: "libraryView"
                to: "visualizerView"
                SequentialAnimation {
                    PropertyAction { target: visualizerView; property: "visible"; value: true }
                    ParallelAnimation {
                        NumberAnimation { target: pathViewScale; properties: "xScale,yScale"; to: 5.0; duration: 600; easing.type: Easing.InQuad }
                        NumberAnimation { target: pathView; property: "opacity"; to: 0.0; duration: 600; easing.type: Easing.InQuad }
                        NumberAnimation { target: gradientRect; property: "opacity"; to: 0.0; duration: 600 }
                        NumberAnimation { target: visualizerView; property: "opacity"; to: 1.0; duration: 600 }
                    }
                    PropertyAction { target: pathView; property: "visible"; value: false }
                }
            },
            Transition {
                from: "visualizerView"
                to: "libraryView"
                SequentialAnimation {
                    PropertyAction { target: pathView; property: "visible"; value: coverFlow.viewMode === "flow" }
                    ParallelAnimation {
                        NumberAnimation { target: pathViewScale; properties: "xScale,yScale"; to: 1.0; duration: 600; easing.type: Easing.OutQuad }
                        NumberAnimation { target: pathView; property: "opacity"; to: 1.0; duration: 600; easing.type: Easing.OutQuad }
                        NumberAnimation { target: gradientRect; property: "opacity"; to: 1.0; duration: 600 }
                        NumberAnimation { target: visualizerView; property: "opacity"; to: 0.0; duration: 600 }
                    }
                    PropertyAction { target: visualizerView; property: "visible"; value: false }
                }
            }
        ]
    }
}
