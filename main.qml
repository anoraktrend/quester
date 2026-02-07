import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects



import Quester 1.0
import Qt.labs.platform 1.1 as Platform

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: qsTr("Quester")
    color: palette.window

    property real fontScale: Math.max(0.8, Math.min(width, height) / 600)
    SystemPalette { id: palette }

    // A HeaderBar provides Client-Side Decorations, which is the standard on
    // many Wayland desktops like GNOME. It also provides a place for window controls.
    HeaderBar {
        id: headerBar
        z: 100
        height: implicitHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        transparentBackground: coverFlow.state === "visualizerView"
        fontScale: window.fontScale

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
            visible: coverFlow.state !== "libraryView"
            width: visible ? implicitWidth : 0
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: menuButton.left
            anchors.rightMargin: 10
            onClicked: presetDialog.open()
        }

        ToolButton {
            id: sortmenuButton
            icon.source: "image://theme/sort-presence"
            icon.color: palette.windowText
            icon.width: 24 * window.fontScale; icon.height: 24 * window.fontScale
            visible: coverFlow.state === "libraryView"
            width: visible ? implicitWidth : 0
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: presetMenuButton.left

            onClicked: sortby.open()
    
            Menu {
                id: sortby
                y: parent.height
                title: qsTr("Sort By")

                 MenuItem {
                    text: qsTr("Artist")
                    checkable: true
                    checked: mpdClient.sortMode === MpdClient.Artist
                    onTriggered: mpdClient.sortMode = MpdClient.Artist
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
        // Application Menu Button (Hamburger Menu)
        ToolButton {
            id: menuButton
            icon.source: "image://theme/application-menu"
            icon.color: palette.windowText
            icon.width: 24 * window.fontScale; icon.height: 24 * window.fontScale
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            onClicked: appMenu.open()

            Menu {
                id: appMenu
                y: parent.height

                MenuItem {
                    text: qsTr("Visualizer")
                    visible: coverFlow.state !== "visualizerView" 
                    height: visible ? implicitHeight : 0
                    onClicked: goToVisualizer()
                }
                
                MenuItem {
                    text: qsTr("Queue")
                    visible: coverFlow.state !== "queueView"
                    height: visible ? implicitHeight : 0
                    onClicked: coverFlow.state = "queueView"
                }

                MenuItem {
                    text: qsTr("Return to Library")
                    visible: coverFlow.state !== "libraryView"
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

                MenuSeparator {
                    visible: coverFlow.state === "libraryView"
                    height: visible ? implicitHeight : 0
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
        state: startInVisualizer ? "visualizerView" : "libraryView"
        
        PathView {
            id: pathView
            visible: coverFlow.viewMode === "flow" && coverFlow.state === "libraryView"
            anchors.top: parent.top
            anchors.topMargin: headerBar.height + 10
            anchors.left: parent.left
            anchors.right: parent.right
            height: 320
            z: 2
            model: mpdClient.albumModel
            pathItemCount: {
                var c = Math.floor(width / 140)
                return Math.max(5, c % 2 === 0 ? c + 1 : c)
            }
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

            delegate: Item {
                width: 200
                height: 200
                visible: coverFlow.viewMode === "flow"
                scale: PathView.iconScale !== undefined ? PathView.iconScale : 1.0
                opacity: PathView.iconOpacity !== undefined ? PathView.iconOpacity : 1.0
                z: PathView.z !== undefined ? PathView.z : 0

                // Reflection
                MultiEffect {
                    id: reflection
                    source: mainContent
                    anchors.top: mainContent.bottom
                    anchors.topMargin: 2
                    width: mainContent.width; height: mainContent.height
                    visible: model.art ? true : false
                    
                    // Flip the reflection
                    transform: Rotation { origin.y: height / 2; angle: 180; axis { x: 1; y: 0; z: 0 } }
                    
                    // Apply iPod-style blur and fade
                    blurEnabled: true
                    blur: 0.5
                    maskEnabled: true
                    maskSource: reflectionMask
                    opacity: 0.4
                }

                Item {
                    id: reflectionMask
                    width: reflection.width; height: reflection.height
                    visible: false
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "white" }
                            GradientStop { position: 0.5; color: "transparent" }
                        }
                    }
                }

                Rectangle {
                    id: mainContent
                    anchors.fill: parent
                    color: palette.base
                    radius: 5
                    border.width: model.art ? 2 : 0 // Hide border when there's no art
                    border.color: palette.accent
                    antialiasing: true
                    clip: true

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
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                contextMenu.popup()
                            } else {
                                mpdClient.playAlbum(model.albumArtist, model.album)
                                pathView.currentIndex = index
                            }
                        }

                        Menu {
                            id: contextMenu
                            MenuItem {
                                text: qsTr("Play Album")
                                onTriggered: mpdClient.playAlbum(model.albumArtist, model.album)
                            }
                            MenuItem {
                                text: qsTr("Add to Queue")
                                onTriggered: mpdClient.addAlbum(model.albumArtist, model.album)
                            }
                        }
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
            anchors.topMargin: headerBar.height + 10
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
                        border.color: palette.accent
                        
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
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    gridContextMenu.popup()
                                } else {
                                    pathView.currentIndex = index
                                    coverFlow.viewMode = "flow"
                                }
                            }

                            Menu {
                                id: gridContextMenu
                                MenuItem {
                                    text: qsTr("Play Album")
                                    onTriggered: mpdClient.playAlbum(model.albumArtist, model.album)
                                }
                                MenuItem {
                                    text: qsTr("Add to Queue")
                                    onTriggered: mpdClient.addAlbum(model.albumArtist, model.album)
                                }
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
                        font.pixelSize: 12 * window.fontScale
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: palette.windowText
                    }
                }
            }
            
            ScrollBar.vertical: ScrollBar { }
        }

        ListView {
            id: browserListView
            anchors.top: parent.top
            anchors.topMargin: headerBar.height + 10
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            clip: true
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "browser"
            model: mpdClient.browserModel
            
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 50
                text: model.name
                icon.source: model.isDir ? "image://theme/folder" : "image://theme/audio-x-generic"
                icon.width: 24
                icon.height: 24
                
                background: Rectangle {
                    color: parent.down ? palette.midlight : (index % 2 == 0 ? palette.base : palette.alternateBase)
                }

                onClicked: {
                    if (model.isDir) {
                        mpdClient.browsePath(model.path)
                    } else {
                        mpdClient.playTrack(model.path)
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: (mouse) => browserContextMenu.popup()
                }

                Menu {
                    id: browserContextMenu
                    MenuItem {
                        text: qsTr("Add to Queue")
                        onTriggered: mpdClient.addPath(model.path)
                    }
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }

        VisualizerView {
            id: visualizerView
            anchors.fill: parent
            magnitudes: AudioVisualizer.magnitudes
            albumArt: mpdClient.albumArt
            contentBottomMargin: 0
            active: (coverFlow.state === "visualizerView" || coverFlow.state === "queueView") && mpdClient.state === "play"
            barOpacity: coverFlow.state === "queueView" ? 0.2 : 0.9
            z: 10
            onClicked: coverFlow.state = "libraryView"

            onWidthChanged: {
                if (width > 0) {
                    AudioVisualizer.width = width;
                }
            }
            
            onHeightChanged: {
                if (height > 0) {
                    AudioVisualizer.height = height;
                }
            }
            
            Component.onCompleted: {
                if (width > 0) {
                    AudioVisualizer.width = width;
                }
                if (height > 0) {
                    AudioVisualizer.height = height;
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

        ListView {
            id: queueListView
            anchors.top: parent.top
            anchors.topMargin: headerBar.height + 10
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            clip: true
            visible: false
            opacity: 0.5
            z: 15 // Above visualizer
            model: mpdClient.queueModel

            delegate: ItemDelegate {
                width: ListView.view.width
                height: 50
                highlighted: model.isCurrent

                background: Rectangle {
                    color: parent.highlighted ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.4) : 
                           (parent.down ? Qt.rgba(palette.midlight.r, palette.midlight.g, palette.midlight.b, 0.4) : "transparent")
                    radius: 4
                }

                contentItem: RowLayout {
                Rectangle {
                        color: parent.highlighted ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.4) : 
                           (parent.down ? Qt.rgba(palette.midlight.r, palette.midlight.g, palette.midlight.b, 0.4) : "transparent")
                        radius: 4
                    }
                    spacing: 10
                    Text {
                        text: model.title
                        color: palette.text
                        font.bold: model.isCurrent
                        font.pixelSize: 14 * window.fontScale
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: model.artist
                        color: palette.windowText
                        font.pixelSize: 12 * window.fontScale
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }
                    Text {
                        text: model.duration
                        color: palette.windowText
                        font.pixelSize: 12 * window.fontScale
                    }
                }

                onClicked: mpdClient.playQueueId(model.id)
                
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: (mouse) => mpdClient.removeId(model.id)
                }
            }
            ScrollBar.vertical: ScrollBar { }
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
                    implicitWidth: 200
                    implicitHeight: 6
                    color: palette.midlight
                    radius: 3
                }
                contentItem: Item {
                    implicitWidth: 200
                    implicitHeight: 6
                    Rectangle {
                        width: progressBar.visualPosition * parent.width
                        height: parent.height
                        radius: 3
                        color: palette.highlight
                    }
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

                Rectangle {
                    Layout.preferredHeight: 64
                    Layout.preferredWidth: 64
                    color: palette.base
                    visible: coverFlow.state === "libraryView"
                    radius: 4
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: mpdClient.albumArt
                        fillMode: Image.PreserveAspectCrop
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: mpdClient.title
                        font.pixelSize: 18 * window.fontScale
                        font.bold: true
                        color: palette.windowText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: mpdClient.artist + " - " + mpdClient.album
                        font.pixelSize: 14 * window.fontScale
                        color: palette.windowText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    spacing: 15
                    Button { 
                        icon.source: "image://theme/media-skip-backward"
                        icon.color: palette.windowText
                        icon.width: 24 * window.fontScale; icon.height: 24 * window.fontScale
                        onClicked: mpdClient.previous()
                        flat: true 
                        background: Rectangle {
                            radius: 20
                            color: "transparent"
                            border.color: palette.accent
                            border.width: 1
                        }
                    }
                    Button {
                        id: playPauseButton
                        icon.source: mpdClient.state === "play" ? "image://theme/media-playback-pause" : "image://theme/media-playback-start"
                        icon.color: palette.windowText
                        icon.width: 24 * window.fontScale; icon.height: 24 * window.fontScale
                        onClicked: mpdClient.togglePlayPause()
                        width: 100; height: 40
                        background: Rectangle {
                            radius: 20
                            color: parent.down ? palette.mid : palette.button
                            border.color: palette.accent
                            border.width: 1
                        }
                    }
                    Button {
                        icon.source: "image://theme/media-skip-forward"
                        icon.color: palette.windowText
                        icon.width: 24 * window.fontScale; icon.height: 24 * window.fontScale
                        onClicked: mpdClient.next()
                        flat: true 
                        background: Rectangle {
                            radius: 20
                            color: "transparent"
                            border.color: palette.accent
                            border.width: 1
                        }
                    }
                }
            }
        }

        ListView {
            id: trackListView
            anchors.top: pathView.bottom
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            model: mpdClient.trackModel
            visible: coverFlow.state === "libraryView" && coverFlow.viewMode === "flow"
            
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 40
                
                background: Rectangle {
                    color: parent.down ? palette.midlight : (index % 2 == 0 ? palette.base : palette.alternateBase)
                }
                
                onClicked: mpdClient.playTrack(model.uri)

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: (mouse) => trackContextMenu.popup()
                }

                Menu {
                    id: trackContextMenu
                    MenuItem {
                        text: qsTr("Add to Queue")
                        onTriggered: mpdClient.addTrack(model.uri)
                    }
                }

                contentItem: Item {
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.title
                        color: palette.text
                        font.pixelSize: 14 * window.fontScale
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.duration
                        color: palette.windowText
                        font.pixelSize: 12 * window.fontScale
                    }
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }

        states: [
            State {
                name: "libraryView"
                PropertyChanges { target: pathView; opacity: 1.0 }
                PropertyChanges { target: pathViewScale; xScale: 1.0; yScale: 1.0 }
                PropertyChanges { target: visualizerView; opacity: 0.0; visible: false }
                PropertyChanges { target: gradientRect; opacity: 1.0 }
            },
            State {
                name: "visualizerView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: pathViewScale; xScale: 5.0; yScale: 5.0 }
                PropertyChanges { target: visualizerView; opacity: 1.0; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
            },
            State {
                name: "queueView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: visualizerView; opacity: 1.0; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
                PropertyChanges { target: queueListView; opacity: 1.0; visible: true }
            }
        ]
    }

    Dialog {
        id: presetDialog
        title: qsTr("Select Preset")
        anchors.centerIn: parent
        width: Math.min(400, parent.width - 40)
        height: Math.min(500, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close
        background: Rectangle {
            color: palette.window
            border.color: palette.midlight
            radius: 10
        }

        ListView {
            anchors.fill: parent
            model: AudioVisualizer.presetNames
            delegate: ItemDelegate {
                width: ListView.view.width
                text: modelData
                highlighted: AudioVisualizer.currentPreset === modelData

                contentItem: Text {
                    text: parent.text
                    font: parent.font
                    color: parent.highlighted ? palette.highlightedText : palette.text
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.highlighted ? palette.highlight : (parent.down ? palette.midlight : "transparent")
                    radius: 5
                }
                onClicked: {
                    AudioVisualizer.currentPreset = modelData
                    presetDialog.close()
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }
    }
}
