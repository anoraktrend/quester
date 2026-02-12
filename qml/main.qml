import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects



import Quester 1.0
import Qt.labs.platform 1.1 as Platform
import QtCore

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: !startMinimized
    title: qsTr("Quester")
    color: palette.window
    visibility: startFullscreen ? ApplicationWindow.FullScreen : (startMinimized ? ApplicationWindow.Minimized : ApplicationWindow.Windowed)

    property real fontScale: Math.max(0.8, Math.min(width, height) / 600)
    property bool useProjectM: false
    property string lastfmAuthToken
    property bool selectionMode: false
    property var selectedAlbums: []
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
        
        viewState: coverFlow.state
        viewMode: coverFlow.viewMode
        useProjectM: window.useProjectM

        onOpenSettings: settingsWindow.show()
        onOpenVisualizerSettings: {
            settingsWindow.defaultTab = 1
            settingsWindow.show()
        }
        onRequestVisualizer: goToVisualizer()
        onRequestQueue: coverFlow.state = "queueView"
        onRequestPlaylists: coverFlow.state = "playlistsView"
        onRequestLibrary: coverFlow.state = "libraryView"
        onRequestWrapped: coverFlow.state = "wrappedView"
        onRequestRefresh: mpdClient.refreshLibrary()
        onRequestAddAllToQueue: mpdClient.addAll()
        onToggleSelectionMode: {
            selectionMode = !selectionMode;
            if (!selectionMode) selectedAlbums = [];
        }
        onToggleProjectM: {
            window.useProjectM = !window.useProjectM
        }
        onSetViewMode: (mode) => { coverFlow.viewMode = mode; if (mode === "browser") mpdClient.browsePath(mpdClient.currentPath) }
    }
    
    Component.onCompleted: {
        mpdClient.consume = true
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
        property string viewMode: startViewMode
        state: {
            if (startView === "visualizer") return "visualizerView"
            if (startView === "queue") return "queueView"
            if (startView === "playlists") return "playlistsView"
            if (startView === "wrapped") return "wrappedView"
            return "libraryView" // Default
        }
        
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
                                mpdClient.playAlbum(model.artist, model.name, model.mbid)
                                pathView.currentIndex = index
                            }
                        }

                        Menu {
                            id: contextMenu
                            MenuItem {
                                text: qsTr("Play Album")
                                onTriggered: mpdClient.playAlbum(model.artist, model.name, model.mbid)
                            }
                            MenuItem {
                                text: qsTr("Add to Queue")
                                onTriggered: mpdClient.addAlbum(model.artist, model.name, model.mbid)
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

            GridViewMode {
                id: gridViewMode
                anchors.top: parent.top
                anchors.topMargin: headerBar.height + 10
                anchors.bottom: bottomControls.top
                anchors.left: parent.left
                anchors.right: parent.right
                visible: coverFlow.state === "libraryView" && (coverFlow.viewMode === "grid" || coverFlow.viewMode === "artists")
                
                selectionMode: window.selectionMode
                selectedAlbums: window.selectedAlbums
                
                // Initialize to appropriate view based on coverFlow.viewMode
                Component.onCompleted: {
                    if (coverFlow.viewMode === "artists") {
                        gridViewMode.viewMode = "artists"
                    } else {
                        gridViewMode.viewMode = "albums"
                    }
                }
                
                // Update view mode when coverFlow.viewMode changes
                Connections {
                    target: coverFlow
                    function onViewModeChanged() {
                        if (coverFlow.viewMode === "artists") {
                            gridViewMode.viewMode = "artists"
                        } else if (coverFlow.viewMode === "grid") {
                            gridViewMode.viewMode = "albums"
                        }
                    }
                }
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
            z: 10
            visible: (coverFlow.state === "visualizerView" || coverFlow.state === "queueView") && mpdClient.state === "play"
            opacity: visible ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 500 } }
            
            albumArt: mpdClient.albumArt
            useProjectM: window.useProjectM
            barOpacity: coverFlow.state !== "visualizerView" ? 0.2 : 1.0
            fallbackColor: palette.text
            
            onClicked: coverFlow.state = "libraryView"
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
                    onClicked: mpdClient.removeId(model.id)
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
            y: parent.height - 100
            z: 20
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: "#80000000" }
                }
            }

            Behavior on y {
             
                NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }



            Slider {
                id: progressBar
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                hoverEnabled: true
                from: 0
                to: mpdClient.duration > 0 ? mpdClient.duration : 1
                
                Binding on value {
                    when: !progressBar.pressed
                    value: mpdClient.elapsed
                }

                onMoved: mpdClient.seek(value)

                background: Rectangle {
                    x: progressBar.leftPadding
                    y: progressBar.topPadding + progressBar.availableHeight / 2 - height / 2
                    implicitWidth: 200
                    implicitHeight: 6
                    width: progressBar.availableWidth
                    height: implicitHeight
                    color: palette.midlight
                    radius: 3
                }

                handle: Rectangle {
                    x: progressBar.leftPadding + progressBar.visualPosition * (progressBar.availableWidth - width)
                    y: progressBar.topPadding + progressBar.availableHeight / 2 - height / 2
                    implicitWidth: 16
                    implicitHeight: 16
                    radius: 8
                    color: palette.highlight
                    border.color: palette.midlight
                    border.width: 1
                    visible: progressBar.pressed || progressBar.hovered
                }

                // Custom progress fill inside the background
                Rectangle {
                    parent: progressBar.background
                    width: progressBar.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: palette.highlight
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
                    id: randomButton
                    icon.source: "image://theme/media-playlist-shuffle"
                    icon.color: mpdClient.random ? palette.highlight : palette.windowText
                    icon.width: 20 * window.fontScale; icon.height: 20 * window.fontScale
                    opacity: mpdClient.random ? 1.0 : 0.6
                    flat: true
                    background: Rectangle { color: "transparent" }
                    onClicked: mpdClient.random = !mpdClient.random
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Shuffle: ") + (mpdClient.random ? qsTr("On") : qsTr("Off"))
                }

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
                Button {
                    id: modeButton
                    property int mode: {
                        if (mpdClient.repeat && mpdClient.single) return 2
                        if (mpdClient.repeat) return 1
                        return 0
                    }
                    icon.source: {
                        switch(mode) {
                            case 1: return "image://theme/media-playlist-repeat"
                            case 2: return "image://theme/media-playlist-repeat-song"
                            default: return "image://theme/media-playlist-repeat"
                        }
                    }
                    icon.color: mode === 0 ? palette.windowText : palette.highlight
                    icon.width: 20 * window.fontScale; icon.height: 20 * window.fontScale
                    opacity: mode === 0 ? 0.6 : 1.0
                    flat: true
                    background: Rectangle { color: "transparent" }
                    onClicked: {
                        if (mode === 0) {
                            mpdClient.repeat = true
                            mpdClient.single = false
                        } else if (mode === 1) {
                            mpdClient.repeat = true
                            mpdClient.single = true
                        } else {
                            mpdClient.repeat = false
                            mpdClient.single = false
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: mode === 0 ? qsTr("Repeat: Off") : (mode === 1 ? qsTr("Repeat: All") : qsTr("Repeat: One"))
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

        RowLayout {
            id: playlistHeader
            anchors.top: parent.top
            anchors.topMargin: headerBar.height + 10
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            visible: coverFlow.state === "playlistsView"
            
            Label { text: qsTr("Playlists"); font.bold: true; color: palette.text }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Save Queue")
                onClicked: {
                    // Save locally first
                    savePlaylistDialog.open()
                }
            }
            Button {
                text: qsTr("Save to ListenBrainz")
                enabled: mpdClient.statistics.credentialsValid && mpdClient.queueModel.count > 0
                onClicked: {
                    lbPlaylistNameField.text = "Quester Playlist " + new Date().toLocaleDateString()
                    lbPlaylistDialog.open()
                }
            }
            Button {
                text: qsTr("Fetch ListenBrainz")
                enabled: mpdClient.statistics.credentialsValid
                onClicked: {
                    mpdClient.statistics.fetchUserPlaylists()
                }
            }
        }

        ListView {
            id: playlistListView
            anchors.top: playlistHeader.bottom
            anchors.topMargin: 10
            anchors.bottom: bottomControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 10
            clip: true
            visible: coverFlow.state === "playlistsView"
            model: mpdClient.playlists
            
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 50

                background: Rectangle {
                    color: parent.down ? palette.midlight : (index % 2 == 0 ? palette.base : palette.alternateBase)
                }

                contentItem: RowLayout {
                    Text {
                        text: modelData
                        color: palette.text
                        font.pixelSize: 14 * window.fontScale
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("Load")
                        onClicked: mpdClient.loadPlaylist(modelData)
                    }
                    Button {
                        text: qsTr("Remove")
                        onClicked: {
                            removePlaylistDialog.playlistName = modelData
                            removePlaylistDialog.open()
                        }
                    }
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }

        WrappedView {
            id: wrappedView
            statistics: mpdClient.statistics
            anchors.fill: parent
            visible: coverFlow.state === "wrappedView"
        }

        states: [
            State {
                name: "libraryView"
                PropertyChanges { target: pathView; opacity: 1.0 }
                PropertyChanges { target: pathViewScale; xScale: 1.0; yScale: 1.0 }
                PropertyChanges { target: visualizerView; opacity: 0.0; visible: false }
                PropertyChanges { target: gradientRect; opacity: 1.0 }
                PropertyChanges { target: wrappedView; visible: false }
                PropertyChanges { target: bottomControls; y: parent.height - 100 }
            },
            State {
                name: "visualizerView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: pathViewScale; xScale: 5.0; yScale: 5.0 }
                PropertyChanges { target: visualizerView; opacity: 1.0; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
                PropertyChanges { target: wrappedView; visible: false }
                PropertyChanges { target: bottomControls; y: parent.height - 100 }
            },
            State {
                name: "queueView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: visualizerView; opacity: 1.0; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
                PropertyChanges { target: queueListView; opacity: 1.0; visible: true }
                PropertyChanges { target: wrappedView; visible: false }
                PropertyChanges { target: bottomControls; y: parent.height - 100 }
            },
            State {
                name: "playlistsView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: visualizerView; opacity: 0.0; visible: false }
                PropertyChanges { target: queueListView; opacity: 0.0; visible: false }
                PropertyChanges { target: wrappedView; visible: false }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
            },
            State {
                name: "wrappedView"
                PropertyChanges { target: pathView; opacity: 0.0 }
                PropertyChanges { target: visualizerView; opacity: 0.0; visible: false }
                PropertyChanges { target: queueListView; opacity: 0.0; visible: false }
                PropertyChanges { target: wrappedView; visible: true }
                PropertyChanges { target: gradientRect; opacity: 0.0 }
            }
        ]
    }

    SettingsWindow { id: settingsWindow; lastfmAuthToken: lastfmAuthToken }

    Dialog {
        id: removePlaylistDialog
        property string playlistName
        title: qsTr("Remove Playlist")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        
        Label {
            text: qsTr("Are you sure you want to remove the playlist '%1'?").arg(removePlaylistDialog.playlistName)
        }

        onAccepted: mpdClient.removePlaylist(playlistName)
    }

    Dialog {
        id: savePlaylistDialog
        title: qsTr("Save Queue as Playlist")
        standardButtons: Dialog.Save | Dialog.Cancel
        modal: true
        
        ColumnLayout {
            Label { text: qsTr("Playlist Name:") }
            TextField { id: playlistNameField }
        }

        onAccepted: mpdClient.savePlaylist(playlistNameField.text)
    }

    Dialog {
        id: lbPlaylistDialog
        title: qsTr("Save Queue to ListenBrainz")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        
        ColumnLayout {
            Label { text: qsTr("Playlist Name:") }
            TextField { id: lbPlaylistNameField }
        }

        onAccepted: {
            var tracks = []
            for (var i = 0; i < mpdClient.queueModel.count; i++) {
                var item = mpdClient.queueModel.get(i)
                tracks.push({
                    title: item.title,
                    artist: item.artist,
                    album: item.album
                })
            }
            mpdClient.statistics.savePlaylistToListenBrainz(lbPlaylistNameField.text, tracks)
        }
    }

    // ListenBrainz playlists model
    ListModel {
        id: lbPlaylistsModel
    }

    // Connection to handle ListenBrainz playlists loaded
    Connections {
        target: mpdClient.statistics
        function onPlaylistsLoaded(playlists) {
            lbPlaylistsModel.clear()
            for (var i = 0; i < playlists.length; i++) {
                lbPlaylistsModel.append(playlists[i])
            }
        }
    }

    // Connection to handle playlist save result
    Connections {
        target: mpdClient.statistics
        function onPlaylistSaved(success, message) {
            if (success) {
                console.log("Playlist saved successfully")
            } else {
                console.log("Playlist save failed:", message)
            }
        }
    }

    // Connection to handle Last.fm auth token
    Connections {
        target: mpdClient.statistics
        function onLastfmAuthTokenReceived(token, authUrl) {
            lastfmAuthToken = token;
            Qt.openUrlExternally(authUrl);
        }
    }

    // Dialog for viewing ListenBrainz playlists
    Dialog {
        id: lbPlaylistsViewDialog
        title: qsTr("ListenBrainz Playlists")
        anchors.centerIn: parent
        width: Math.min(500, parent.width - 40)
        height: Math.min(400, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close

        ListView {
            id: lbPlaylistsListView
            anchors.fill: parent
            model: lbPlaylistsModel
            delegate: Rectangle {
                width: ListView.view.width
                height: 60
                color: index % 2 === 0 ? palette.base : palette.alternateBase

                readonly property bool highlighted: ListView.isCurrentItem

                Rectangle {
                    anchors.fill: parent
                    color: highlighted ? palette.highlight : "transparent"
                    opacity: highlighted ? 0.3 : 0
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    Text {
                        text: model.name
                        font.bold: true
                        color: highlighted ? palette.highlightedText : palette.text
                    }
                    Text {
                        text: model.creator + " - " + model.track_count + " tracks"
                        font.pixelSize: 12
                        color: highlighted ? palette.highlightedText : palette.windowText
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        lbPlaylistsListView.currentIndex = index
                        Qt.openUrlExternally(model.url)
                    }
                }
            }
        }
    }

    Platform.FolderDialog {
        id: folderDialog
        title: qsTr("Select ProjectM Presets Folder")
        onAccepted: {
            var path = folder.toString()
            if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            visualizerView.settings.projectMPresetPath = path
        }
    }

    // Dialog for viewing JSPF playlist details with save button
    Dialog {
        id: jspfPlaylistDialog
        property string currentIdentifier: ""
        title: qsTr("JSPF Playlist")
        anchors.centerIn: parent
        width: Math.min(600, parent.width - 40)
        height: Math.min(500, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            // Playlist info header
            RowLayout {
                Layout.fillWidth: true
                Label {
                    id: jspfPlaylistTitle
                    text: qsTr("Playlist")
                    font.bold: true
                    font.pixelSize: 16
                    color: palette.text
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Save to Cache")
                    onClicked: {
                        if (jspfPlaylistDialog.currentIdentifier.length > 0) {
                            mpdClient.saveJspfPlaylistToCache(jspfPlaylistDialog.currentIdentifier)
                        }
                    }
                }
            }

            Label {
                id: jspfPlaylistCreator
                text: qsTr("by Unknown")
                color: palette.windowText
                font.pixelSize: 12
            }

            // Tracks list
            ListView {
                id: jspfTrackListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: mpdClient.playlistTrackModel

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 40
                    color: index % 2 === 0 ? palette.base : palette.alternateBase

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text {
                            text: model.title
                            color: palette.text
                            font.pixelSize: 14
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Text {
                            text: model.creator
                            color: palette.windowText
                            font.pixelSize: 12
                            Layout.preferredWidth: 100
                            elide: Text.ElideRight
                        }
                        Text {
                            text: model.duration
                            color: palette.windowText
                            font.pixelSize: 12
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }
        }
    }

    // Connection to handle playlist saved notification
    Connections {
        target: mpdClient
        function onPlaylistSaved(title, path) {
            console.log("Playlist saved:", title, "to", path)
            // Could show a toast notification here
        }
    }

    // --- Deduplicator Dialog ---
    Dialog {
        id: deduplicatorDialog
        title: qsTr("Find Duplicates")
        anchors.centerIn: parent
        width: Math.min(700, parent.width - 40)
        height: Math.min(600, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close

        property var duplicatesList: []
        property var urisToDelete: []

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                text: qsTr("Find and remove duplicate tracks from your library.")
                color: palette.windowText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Scan for Duplicates")
                onClicked: {
                    mpdClient.findDuplicates()
                    deduplicatorStatus.text = qsTr("Scanning...")
                }
                Layout.fillWidth: true
            }

            Label {
                id: deduplicatorStatus
                text: qsTr("Ready to scan")
                color: palette.text
                font.bold: true
                Layout.fillWidth: true
            }

            // Duplicates list
            ListView {
                id: duplicatesListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: deduplicatorDialog.duplicatesList.length

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 80
                    color: palette.base
                    radius: 5

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: modelData.title
                                font.bold: true
                                color: palette.text
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: qsTr("%n duplicate(s)", "", modelData.count)
                                color: palette.highlight
                                font.bold: true
                            }
                        }

                        Text {
                            text: modelData.artist + " - " + modelData.album + " (" + modelData.duration + ")"
                            color: palette.windowText
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            CheckBox {
                                id: selectCheckbox
                                text: qsTr("Keep first")
                                checked: true
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                text: qsTr("View Paths")
                                onClicked: {
                                    duplicatePathsDialog.duplicateData = modelData
                                    duplicatePathsDialog.open()
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            // Toggle selection logic handled in deleteSelectedDuplicates
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }

            // Summary and delete button
            RowLayout {
                Layout.fillWidth: true

                Label {
                    id: deleteSummary
                    text: qsTr("Select duplicates above to delete")
                    color: palette.windowText
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Delete Selected")
                    enabled: deduplicatorDialog.urisToDelete.length > 0
                    onClicked: {
                        deleteConfirmDialog.open()
                    }
                }
            }
        }

        onDuplicatesListChanged: {
            var count = deduplicatorDialog.duplicatesList.length
            deduplicatorStatus.text = qsTr("Found %n duplicate group(s)", "", count)
        }

        Connections {
            target: mpdClient
            function onDuplicatesFound(duplicates) {
                deduplicatorDialog.duplicatesList = duplicates
                deduplicatorDialog.urisToDelete = []
                if (duplicates.length === 0) {
                    deduplicatorStatus.text = qsTr("No duplicates found!")
                } else {
                    deduplicatorStatus.text = qsTr("Found %n duplicate group(s)", "", duplicates.length)
                }
            }

            function onDuplicatesDeleted(count) {
                deduplicatorStatus.text = qsTr("Deleted %n track(s)", "", count)
                deduplicatorDialog.urisToDelete = []
                deduplicatorDialog.duplicatesList = []
            }
        }
    }

    // Dialog to show paths for a duplicate group
    Dialog {
        id: duplicatePathsDialog
        property var duplicateData: null
        title: qsTr("Duplicate Files")
        anchors.centerIn: parent
        width: Math.min(600, parent.width - 40)
        height: Math.min(400, parent.height - 40)
        modal: true
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                text: duplicatePathsDialog.duplicateData ? (duplicatePathsDialog.duplicateData.artist + " - " + duplicatePathsDialog.duplicateData.title) : ""
                font.bold: true
                color: palette.text
                Layout.fillWidth: true
            }

            ListView {
                id: pathsListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: duplicatePathsDialog.duplicateData ? duplicatePathsDialog.duplicateData.uris : []

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 40
                    color: index % 2 === 0 ? palette.base : palette.alternateBase

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        CheckBox {
                            id: pathCheckbox
                            checked: index > 0  // Uncheck first (keep it), check rest for deletion
                            onCheckedChanged: {
                                if (duplicatePathsDialog.visible && checked) {
                                    var uris = deduplicatorDialog.urisToDelete
                                    var uri = modelData
                                    if (!uris.includes(uri)) {
                                        uris.push(uri)
                                        deduplicatorDialog.urisToDelete = uris
                                    }
                                } else if (!checked) {
                                    var uris = deduplicatorDialog.urisToDelete
                                    var index = uris.indexOf(modelData)
                                    if (index > -1) {
                                        uris.splice(index, 1)
                                        deduplicatorDialog.urisToDelete = uris
                                    }
                                }
                                updateDeleteSummary()
                            }
                        }

                        Text {
                            text: modelData
                            color: palette.text
                            font.pixelSize: 11
                            elide: Text.ElideLeft
                            Layout.fillWidth: true
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }

            Label {
                text: qsTr("Uncheck files you want to KEEP. Check files you want to DELETE.")
                color: palette.highlight
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        function updateDeleteSummary() {
            var count = deduplicatorDialog.urisToDelete.length
            deleteSummary.text = qsTr("%n file(s) selected for deletion", "", count)
        }
    }

    // Confirmation dialog for deletion
    Dialog {
        id: deleteConfirmDialog
        title: qsTr("Confirm Deletion")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Are you sure you want to delete %n track(s)?", "", deduplicatorDialog.urisToDelete.length)
                font.bold: true
                color: palette.text
            }

            Label {
                text: qsTr("This will remove the files from disk and update MPD.")
                color: palette.windowText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        onAccepted: {
            mpdClient.deleteSelectedDuplicates(deduplicatorDialog.urisToDelete)
        }
    }
}
