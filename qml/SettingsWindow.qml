import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Quester 1.0
import net.helltop.quester

Window {
    title: qsTr("Settings")
    width: 500
    height: 500
    visible: false
    color: palette.window
    property int defaultTab: 0
    property string lastfmAuthToken: ""

    onVisibleChanged: {
        if (visible) {
            settingsTabBar.currentIndex = defaultTab
        }
    }

    SystemPalette { id: palette }
    Settings { id: settings }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        TabBar {
            id: settingsTabBar
            Layout.fillWidth: true
            TabButton { text: qsTr("General") }
            TabButton { text: qsTr("Visualizer") }
            TabButton { text: qsTr("ProjectM") }
        }

        StackLayout {
            currentIndex: settingsTabBar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // General Tab
            Item {
                ScrollView {
                    anchors.fill: parent
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Label { text: qsTr("Sort Library By:"); font.bold: true; color: palette.text }
                        RadioButton {
                            text: qsTr("Artist")
                            checked: mpdClient.sortMode === MpdClient.AlbumArtist
                            onClicked: mpdClient.sortMode = MpdClient.AlbumArtist
                        }
                        RadioButton {
                            text: qsTr("Album")
                            checked: mpdClient.sortMode === MpdClient.Album
                            onClicked: mpdClient.sortMode = MpdClient.Album
                        }
                        RadioButton {
                            text: qsTr("Artist & Year")
                            checked: mpdClient.sortMode === MpdClient.ArtistYear
                            onClicked: mpdClient.sortMode = MpdClient.ArtistYear
                        }

                        CheckBox {
                            text: qsTr("Consume Mode")
                            checked: mpdClient.consume
                            onClicked: mpdClient.consume = checked
                        }
                        
                        Button {
                            text: qsTr("Refresh Library")
                            onClicked: mpdClient.refreshLibrary()
                        }

                        Label { text: qsTr("ListenBrainz Settings"); font.bold: true; color: palette.text }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: listenbrainzUsernameField
                                placeholderText: qsTr("ListenBrainz Username")
                                text: mpdClient.listenBrainzUsername
                                onTextChanged: {
                                    mpdClient.listenBrainzUsername = text
                                }
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: mpdClient.statistics.credentialsValid ? "#4CAF50" : "#F44336"
                            }
                        }
                        TextField {
                            id: listenbrainzTokenField
                            placeholderText: qsTr("ListenBrainz Token")
                            text: mpdClient.listenBrainzToken
                            onTextChanged: {
                                mpdClient.listenBrainzToken = text
                            }
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                        Button {
                            text: qsTr("Connect ListenBrainz Account")
                            onClicked: {
                                if (listenbrainzUsernameField.text && listenbrainzTokenField.text) {
                                    mpdClient.listenBrainzUsername = listenbrainzUsernameField.text
                                    mpdClient.listenBrainzToken = listenbrainzTokenField.text
                                    mpdClient.statistics.validateListenBrainzCredentials()
                                }
                            }
                            Layout.fillWidth: true
                        }

                        Label { text: qsTr("Last.fm Authentication"); font.bold: true; color: palette.text }
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                width: 12
                                height: 12
                                radius: 6
                                color: mpdClient.statistics.lastfmCredentialsValid ? "#4CAF50" : "#F44336"
                            }
                            Label {
                                text: mpdClient.statistics.lastfmUsername
                                visible: mpdClient.statistics.lastfmCredentialsValid
                            }
                        }
                        Button {
                            text: qsTr("Authenticate Last.fm")
                            onClicked: {
                                mpdClient.statistics.startLastfmAuth()
                            }
                            Layout.fillWidth: true
                            visible: !mpdClient.statistics.lastfmCredentialsValid
                        }
                        Button {
                            text: qsTr("Deauthenticate Last.fm")
                            onClicked: {
                                // Clear Last.fm credentials
                                mpdClient.statistics.setLastfmCredentials("", "", "")
                            }
                            Layout.fillWidth: true
                            visible: mpdClient.statistics.lastfmCredentialsValid
                        }
                    }
                }
            }

            // Visualizer Tab
            Item {
                ScrollView {
                    anchors.fill: parent
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        anchors.margins: 10
                        spacing: 10

                        Label { 
                            text: qsTr("Visualizer Settings") 
                            font.bold: true 
                            color: palette.text
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 10
                            Layout.fillWidth: true

                            Label { text: qsTr("Color Preset:") }
                            ComboBox {
                                model: AudioVisualizer.presetNames
                                currentIndex: model.indexOf(AudioVisualizer.currentPreset)
                                onActivated: AudioVisualizer.currentPreset = currentText
                                Layout.fillWidth: true
                            }

                            Label { text: qsTr("Appearance:") }
                            ComboBox {
                                model: [qsTr("Bottom Up"), qsTr("Top Down"), qsTr("Centered")]
                                currentIndex: visualizerView.settings.visualizerMode
                                onActivated: visualizerView.settings.visualizerMode = currentIndex
                            }
                        }
                        
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            // ProjectM Tab
            Item {
                ScrollView {
                    anchors.fill: parent
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 10

                        Label { 
                            text: qsTr("ProjectM Preset Path") 
                            color: palette.text
                            font.bold: true 
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: presetPathField
                                text: visualizerView.settings.projectMPresetPath
                                placeholderText: qsTr("Default")
                                readOnly: true
                                Layout.fillWidth: true
                                color: palette.text
                                background: Rectangle { color: palette.base; border.color: palette.mid }
                            }
                            Button {
                                text: qsTr("Browse...")
                                onClicked: folderDialog.open()
                            }
                        }
                        
                        Label {
                            text: qsTr("Note: Changes require restarting the visualizer (toggle off/on).")
                            color: palette.windowText
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 10
                            Layout.fillWidth: true

                            Label { text: qsTr("Texture Size:") }
                            ComboBox {
                                model: [1024, 2048, 4096]
                                currentIndex: model.indexOf(visualizerView.settings.projectMTextureSize)
                                onCurrentIndexChanged: visualizerView.settings.projectMTextureSize = model[currentIndex]
                            }

                            Label { text: qsTr("Mesh X:") }
                            SpinBox {
                                from: 16
                                to: 256
                                value: visualizerView.settings.projectMMeshX
                                onValueChanged: visualizerView.settings.projectMMeshX = value
                            }

                            Label { text: qsTr("Mesh Y:") }
                            SpinBox {
                                from: 12
                                to: 192
                                value: visualizerView.settings.projectMMeshY
                                onValueChanged: visualizerView.settings.projectMMeshY = value
                            }

                            Label { text: qsTr("FPS:") }
                            SpinBox {
                                from: 15
                                to: 60
                                value: visualizerView.settings.projectMFPS
                                onValueChanged: visualizerView.settings.projectMFPS = value
                            }

                            Label { text: qsTr("Smooth Preset Duration:") }
                            SpinBox {
                                from: 0
                                to: 30
                                value: visualizerView.settings.projectMSmoothPresetDuration
                                onValueChanged: visualizerView.settings.projectMSmoothPresetDuration = value
                            }

                            Label { text: qsTr("Preset Duration:") }
                            SpinBox {
                                from: 5
                                to: 600
                                value: visualizerView.settings.projectMPresetDuration
                                onValueChanged: visualizerView.settings.projectMPresetDuration = value
                            }
                            
                            Label { text: qsTr("Beat Sensitivity:") }
                            RowLayout {
                                Layout.fillWidth: true
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    value: visualizerView.settings.projectMBeatSensitivity
                                    onValueChanged: visualizerView.settings.projectMBeatSensitivity = value
                                }
                                Label {
                                    text: visualizerView.settings.projectMBeatSensitivity.toFixed(1)
                                }
                            }

                            CheckBox {
                                text: qsTr("Shuffle Enabled")
                                checked: visualizerView.settings.projectMShuffleEnabled
                                onClicked: visualizerView.settings.projectMShuffleEnabled = checked
                                Layout.columnSpan: 2
                            }

                            CheckBox {
                                text: qsTr("Show Bars with ProjectM")
                                checked: visualizerView.settings.projectMShowBars
                                onClicked: visualizerView.settings.projectMShowBars = checked
                                Layout.columnSpan: 2
                            }

                            Label { 
                                text: qsTr("Bar Opacity:") 
                                visible: visualizerView.settings.projectMShowBars
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: visualizerView.settings.projectMShowBars
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0.1
                                    to: 1.0
                                    value: visualizerView.settings.projectMBarOpacity
                                    onValueChanged: visualizerView.settings.projectMBarOpacity = value
                                }
                                Label { text: visualizerView.settings.projectMBarOpacity.toFixed(1) }
                            }

                            Label { text: qsTr("Selected Preset:") }
                            ComboBox {
                                id: presetComboBox
                                model: visualizerView.presetModel
                                currentIndex: -1
                                Layout.columnSpan: 2
                                width: parent.width
                                
                                onModelChanged: {
                                    if (visualizerView.settings.projectMSelectedPreset) {
                                        var idx = model.indexOf(visualizerView.settings.projectMSelectedPreset)
                                        if (idx >= 0) currentIndex = idx
                                    }
                                }

                                onCurrentIndexChanged: {
                                    if (currentIndex >= 0) {
                                        visualizerView.settings.projectMSelectedPreset = model[currentIndex]
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: mpdClient.statistics
        function onLastfmAuthTokenReceived(token, authUrl) {
            lastfmAuthToken = token
            console.log("[QML] Last.fm auth URL:", authUrl)
            Qt.openUrlExternally(authUrl)
        }
    }
}
