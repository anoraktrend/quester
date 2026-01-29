import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls



ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: qsTr("Quester")
    color: "#1E1E1E"

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

        // The library button from the main view is moved here for a cleaner look.
        Button {
            id: libraryButton
            text: coverFlow.state === "libraryView" ? qsTr("Refresh Library") : qsTr("Return to Library")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                if (coverFlow.state === "libraryView") {
                    mpdClient.refreshLibrary()
                } else {
                    coverFlow.state = "libraryView"
                }
            }
        }

        // Application Menu Button (Hamburger Menu)
        ToolButton {
            text: "\u2630" // Hamburger icon
            font.pixelSize: 20
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            Menu {
                id: appMenu
                x: parent.width - width

                MenuItem { action: fullscreenAction }
                MenuItem { action: quitAction }
            }

            onClicked: appMenu.open()
        }
    }
    
    Component.onCompleted: {
        audioVisualiser.start()
    }

    Item {
        id: coverFlow
        anchors.fill: parent
        state: "libraryView"
        
        PathView {
            id: pathView
            // The HeaderBar is part of the ApplicationWindow chrome, so the content area starts at the top.
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.right: parent.right
            height: 250
            model: mpdClient.albumModel
            pathItemCount: 5
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
            

            delegate: Rectangle {
                width: 200
                height: 200
                color: "#333"
                scale: PathView.iconScale
                opacity: PathView.iconOpacity
                z: PathView.z
                radius: 5
                border.width: model.art ? 2 : 0 // Hide border when there's no art
                border.color: "white"
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
                    color: "white"
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

        Rectangle {
            id: gradientRect
            anchors.left: pathView.left
            anchors.right: pathView.right
            anchors.bottom: pathView.bottom
            height: 250
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 1.0; color: "#1E1E1E" }
            }
            z: pathView.z - 1
        }

        Item {
            id: bottomControls
            x: 0
            width: parent.width
            height: 180
            y: parent.height - height

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
                    color: "#80000000" // Darker than background
                }
            }

            Item {
                id: visualizer
                anchors.left: progressBar.left
                anchors.right: progressBar.right
                anchors.top: progressBar.top
                anchors.bottom: progressBar.bottom
                z: progressBar.z - 1 // Draw behind the progress bar's content

                ShaderEffect {
                    anchors.fill: parent
                    property variant bars: audioVisualiser.magnitudes
                    property int barCount: 32

                    // This rectangle provides the gradient for the shader.
                    // By being a child of ShaderEffect with visible:false,
                    // it automatically becomes the 'source' texture.
                    Rectangle {
                        id: visualizerSource
                        width: visualizer.width; height: visualizer.height
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#03A3E6" }
                            GradientStop { position: 0.25; color: "#F8B4CD" }
                            GradientStop { position: 0.50; color: "#FAFBF9" }
                            GradientStop { position: 0.75; color: "#FA9C57" }
                            GradientStop { position: 1.0; color: "#A80864" }
                        }
                        visible: false
                    }

                    // Pack the 32 bars into 8 vec4s for the shader
                    property vector4d freqs1: Qt.vector4d(bars.length > 3 ? bars[0] : 0, bars.length > 3 ? bars[1] : 0, bars.length > 3 ? bars[2] : 0, bars.length > 3 ? bars[3] : 0)
                    property vector4d freqs2: Qt.vector4d(bars.length > 7 ? bars[4] : 0, bars.length > 7 ? bars[5] : 0, bars.length > 7 ? bars[6] : 0, bars.length > 7 ? bars[7] : 0)
                    property vector4d freqs3: Qt.vector4d(bars.length > 11 ? bars[8] : 0, bars.length > 11 ? bars[9] : 0, bars.length > 11 ? bars[10] : 0, bars.length > 11 ? bars[11] : 0)
                    property vector4d freqs4: Qt.vector4d(bars.length > 15 ? bars[12] : 0, bars.length > 15 ? bars[13] : 0, bars.length > 15 ? bars[14] : 0, bars.length > 15 ? bars[15] : 0)
                    property vector4d freqs5: Qt.vector4d(bars.length > 19 ? bars[16] : 0, bars.length > 19 ? bars[17] : 0, bars.length > 19 ? bars[18] : 0, bars.length > 19 ? bars[19] : 0)
                    property vector4d freqs6: Qt.vector4d(bars.length > 23 ? bars[20] : 0, bars.length > 23 ? bars[21] : 0, bars.length > 23 ? bars[22] : 0, bars.length > 23 ? bars[23] : 0)
                    property vector4d freqs7: Qt.vector4d(bars.length > 27 ? bars[24] : 0, bars.length > 27 ? bars[25] : 0, bars.length > 27 ? bars[26] : 0, bars.length > 27 ? bars[27] : 0)
                    property vector4d freqs8: Qt.vector4d(bars.length > 31 ? bars[28] : 0, bars.length > 31 ? bars[29] : 0, bars.length > 31 ? bars[30] : 0, bars.length > 31 ? bars[31] : 0)

                    fragmentShader: "qrc:/shaders/visualizer.frag.qsb"
                }
            }

            ColumnLayout {
                anchors.top: progressBar.bottom
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 15

                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 5
                    Text {
                        text: mpdClient.title
                        font.pixelSize: 20
                        color: "white"
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: mpdClient.artist + " - " + mpdClient.album
                        font.pixelSize: 16
                        color: "#B0B0B0"
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 20
                    Button { text: "◀◀"; onClicked: mpdClient.previous() }
                    Button {
                        id: playPauseButton
                        text: mpdClient.state === "play" ? "❚❚" : "▶"
                        onClicked: mpdClient.togglePlayPause()
                        font.pixelSize: 20
                        width: 60; height: 60
                        background: Rectangle {
                            color: playPauseButton.down ? "#333" : "#555"
                            radius: 30
                        }
                    }
                    Button { text: "▶▶"; onClicked: mpdClient.next() }
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
            clip: true
            
            delegate: Item {
                width: parent.width
                height: 40
                
                Rectangle {
                    anchors.fill: parent
                    color: index % 2 == 0 ? "#222" : "#1a1a1a"
                    
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
                    color: "white"
                    font.pixelSize: 14
                }
                
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.duration
                    color: "#aaa"
                    font.pixelSize: 12
                }
            }
        }
    }
}
