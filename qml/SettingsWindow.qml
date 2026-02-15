import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Quester 1.0
import net.helltop.quester
import org.kde.kirigami as Kirigami

Window {
    property int defaultTab: 0
    property string lastfmAuthToken: ""

    // Store initial values for cancel functionality
    property var initialValues: ({})

    title: qsTr("Settings")
    width: 500
    height: 500
    visible: false
    color: Kirigami.Theme.backgroundColor
    onVisibleChanged: {
        if (visible) {
            settingsTabBar.currentIndex = defaultTab;
            storeInitialValues();
        }
    }

    function storeInitialValues() {
        initialValues = {
            // General settings
            "sortMode": mpdClient.sortMode,
            "consume": mpdClient.consume,
            "listenBrainzUsername": mpdClient.listenBrainzUsername,
            "listenBrainzToken": mpdClient.listenBrainzToken,
            // Visualizer settings
            "visualizerMode": visualizerView.settings.visualizerMode,
            "visualizerBarSize": visualizerView.settings.visualizerBarSize,
            "visualizerBarGap": visualizerView.settings.visualizerBarGap,
            "visualizerBarOpacity": visualizerView.settings.visualizerBarOpacity,
            // ProjectM settings
            "projectMPresetPath": visualizerView.settings.projectMPresetPath,
            "projectMTextureSize": visualizerView.settings.projectMTextureSize,
            "projectMMeshX": visualizerView.settings.projectMMeshX,
            "projectMMeshY": visualizerView.settings.projectMMeshY,
            "projectMFPS": visualizerView.settings.projectMFPS,
            "projectMSmoothPresetDuration": visualizerView.settings.projectMSmoothPresetDuration,
            "projectMPresetDuration": visualizerView.settings.projectMPresetDuration,
            "projectMBeatSensitivity": visualizerView.settings.projectMBeatSensitivity,
            "projectMShuffleEnabled": visualizerView.settings.projectMShuffleEnabled,
            "projectMSelectedPreset": visualizerView.settings.projectMSelectedPreset,
            "projectMShowBars": visualizerView.settings.projectMShowBars,
            "projectMBarOpacity": visualizerView.settings.projectMBarOpacity
        };
    }

    function restoreInitialValues() {
        // General settings
        mpdClient.sortMode = initialValues.sortMode;
        mpdClient.consume = initialValues.consume;
        mpdClient.listenBrainzUsername = initialValues.listenBrainzUsername;
        mpdClient.listenBrainzToken = initialValues.listenBrainzToken;
        // Visualizer settings
        visualizerView.settings.visualizerMode = initialValues.visualizerMode;
        visualizerView.settings.visualizerBarSize = initialValues.visualizerBarSize;
        visualizerView.settings.visualizerBarGap = initialValues.visualizerBarGap;
        visualizerView.settings.visualizerBarOpacity = initialValues.visualizerBarOpacity;
        // ProjectM settings
        visualizerView.settings.projectMPresetPath = initialValues.projectMPresetPath;
        visualizerView.settings.projectMTextureSize = initialValues.projectMTextureSize;
        visualizerView.settings.projectMMeshX = initialValues.projectMMeshX;
        visualizerView.settings.projectMMeshY = initialValues.projectMMeshY;
        visualizerView.settings.projectMFPS = initialValues.projectMFPS;
        visualizerView.settings.projectMSmoothPresetDuration = initialValues.projectMSmoothPresetDuration;
        visualizerView.settings.projectMPresetDuration = initialValues.projectMPresetDuration;
        visualizerView.settings.projectMBeatSensitivity = initialValues.projectMBeatSensitivity;
        visualizerView.settings.projectMShuffleEnabled = initialValues.projectMShuffleEnabled;
        visualizerView.settings.projectMSelectedPreset = initialValues.projectMSelectedPreset;
        visualizerView.settings.projectMShowBars = initialValues.projectMShowBars;
        visualizerView.settings.projectMBarOpacity = initialValues.projectMBarOpacity;
    }

    Settings {
        id: settings
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        TabBar {
            id: settingsTabBar

            Layout.fillWidth: true

            TabButton {
                text: qsTr("General")
            }

            TabButton {
                text: qsTr("Visualizer")
            }

            TabButton {
                text: qsTr("ProjectM")
            }

        }

        StackLayout {
            currentIndex: settingsTabBar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // General Tab
            Kirigami.FormLayout {
                Kirigami.Heading {
                    level: 4
                    text: qsTr("Sort Library By:")
                }

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

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    level: 4
                    text: qsTr("ListenBrainz Settings")
                }

                TextField {
                    id: listenbrainzUsernameField

                    placeholderText: qsTr("ListenBrainz Username")
                    text: mpdClient.listenBrainzUsername
                    onTextChanged: mpdClient.listenBrainzUsername = text
                }

                TextField {
                    id: listenbrainzTokenField

                    placeholderText: qsTr("ListenBrainz Token")
                    text: mpdClient.listenBrainzToken
                    onTextChanged: mpdClient.listenBrainzToken = text
                    echoMode: TextField.Password
                }

                Button {
                    text: qsTr("Connect ListenBrainz Account")
                    onClicked: {
                        if (listenbrainzUsernameField.text && listenbrainzTokenField.text) {
                            mpdClient.listenBrainzUsername = listenbrainzUsernameField.text;
                            mpdClient.listenBrainzToken = listenbrainzTokenField.text;
                            mpdClient.statistics.validateListenBrainzCredentials();
                        }
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    level: 4
                    text: qsTr("Last.fm Authentication")
                }

                Button {
                    text: qsTr("Authenticate Last.fm")
                    onClicked: mpdClient.statistics.startLastfmAuth()
                    visible: !mpdClient.statistics.lastfmCredentialsValid
                }

                Button {
                    text: qsTr("Deauthenticate Last.fm")
                    onClicked: mpdClient.statistics.setLastfmCredentials("", "", "")
                    visible: mpdClient.statistics.lastfmCredentialsValid
                }

            }

            // Visualizer Tab
            Kirigami.FormLayout {
                Kirigami.Heading {
                    level: 4
                    text: qsTr("Visualizer Settings")
                }

                Label {
                    text: qsTr("Color Preset:")
                }

                ComboBox {
                    model: AudioVisualizer.presetNames
                    currentIndex: model.indexOf(AudioVisualizer.currentPreset)
                    onActivated: AudioVisualizer.currentPreset = currentText
                }

                Label {
                    text: qsTr("Appearance:")
                }

                ComboBox {
                    model: [qsTr("Bottom Up"), qsTr("Top Down"), qsTr("Centered")]
                    currentIndex: visualizerView.settings.visualizerMode
                    onActivated: visualizerView.settings.visualizerMode = currentIndex
                }

                Label {
                    text: qsTr("Bar Size:")
                }

                SpinBox {
                    from: 1
                    to: 100
                    value: visualizerView.settings.visualizerBarSize
                    onValueChanged: visualizerView.settings.visualizerBarSize = value
                }

                Label {
                    text: qsTr("Bar Gap:")
                }

                SpinBox {
                    from: 0
                    to: 50
                    value: visualizerView.settings.visualizerBarGap
                    onValueChanged: visualizerView.settings.visualizerBarGap = value
                }

                Label {
                    text: qsTr("Bar Opacity:")
                }

                Slider {
                    from: 0.1
                    to: 1
                    value: visualizerView.settings.visualizerBarOpacity
                    onValueChanged: visualizerView.settings.visualizerBarOpacity = value
                }

            }

            // ProjectM Tab
            Kirigami.FormLayout {
                Kirigami.Heading {
                    level: 4
                    text: qsTr("ProjectM Preset Path")
                }

                RowLayout {
                    Layout.fillWidth: true

                    TextField {
                        id: presetPathField

                        text: visualizerView.settings.projectMPresetPath
                        placeholderText: qsTr("Default")
                        readOnly: true
                        Layout.fillWidth: true
                    }

                    Button {
                        text: qsTr("Browse...")
                        onClicked: folderDialog.open()
                    }

                }

                Kirigami.InlineMessage {
                    text: qsTr("Note: Changes require restarting the visualizer (toggle off/on).")
                    type: Kirigami.MessageType.Information
                }

                Label {
                    text: qsTr("Texture Size:")
                }

                ComboBox {
                    model: [1024, 2048, 4096]
                    currentIndex: model.indexOf(visualizerView.settings.projectMTextureSize)
                    onCurrentIndexChanged: visualizerView.settings.projectMTextureSize = model[currentIndex]
                }

                Label {
                    text: qsTr("Mesh X:")
                }

                SpinBox {
                    from: 16
                    to: 256
                    value: visualizerView.settings.projectMMeshX
                    onValueChanged: visualizerView.settings.projectMMeshX = value
                }

                Label {
                    text: qsTr("Mesh Y:")
                }

                SpinBox {
                    from: 12
                    to: 192
                    value: visualizerView.settings.projectMMeshY
                    onValueChanged: visualizerView.settings.projectMMeshY = value
                }

                Label {
                    text: qsTr("FPS:")
                }

                SpinBox {
                    from: 15
                    to: 60
                    value: visualizerView.settings.projectMFPS
                    onValueChanged: visualizerView.settings.projectMFPS = value
                }

                Label {
                    text: qsTr("Smooth Preset Duration:")
                }

                SpinBox {
                    from: 0
                    to: 30
                    value: visualizerView.settings.projectMSmoothPresetDuration
                    onValueChanged: visualizerView.settings.projectMSmoothPresetDuration = value
                }

                Label {
                    text: qsTr("Preset Duration:")
                }

                SpinBox {
                    from: 5
                    to: 600
                    value: visualizerView.settings.projectMPresetDuration
                    onValueChanged: visualizerView.settings.projectMPresetDuration = value
                }

                Label {
                    text: qsTr("Beat Sensitivity:")
                }

                Slider {
                    from: 0
                    to: 100
                    value: visualizerView.settings.projectMBeatSensitivity
                    onValueChanged: visualizerView.settings.projectMBeatSensitivity = value
                }

                CheckBox {
                    text: qsTr("Shuffle Enabled")
                    checked: visualizerView.settings.projectMShuffleEnabled
                    onClicked: visualizerView.settings.projectMShuffleEnabled = checked
                }

                CheckBox {
                    text: qsTr("Show Bars with ProjectM")
                    checked: visualizerView.settings.projectMShowBars
                    onClicked: visualizerView.settings.projectMShowBars = checked
                }

                Label {
                    text: qsTr("Bar Opacity:")
                    visible: visualizerView.settings.projectMShowBars
                }

                Slider {
                    visible: visualizerView.settings.projectMShowBars
                    from: 0.1
                    to: 1
                    value: visualizerView.settings.projectMBarOpacity
                    onValueChanged: visualizerView.settings.projectMBarOpacity = value
                }

                Label {
                    text: qsTr("Selected Preset:")
                }

                ComboBox {
                    id: presetComboBox

                    model: visualizerView.presetModel
                    currentIndex: -1
                    onModelChanged: {
                        if (visualizerView.settings.projectMSelectedPreset) {
                            var idx = model.indexOf(visualizerView.settings.projectMSelectedPreset);
                            if (idx >= 0)
                                currentIndex = idx;

                        }
                    }
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0)
                            visualizerView.settings.projectMSelectedPreset = model[currentIndex];

                    }
                }

            }

        }

        // Action buttons at the bottom
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.mediumSpacing

            Button {
                text: qsTr("Cancel")
                onClicked: {
                    restoreInitialValues();
                    settingsWindow.visible = false;
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Apply")
                onClicked: {
                    // Settings are applied immediately, just store the new initial values
                    storeInitialValues();
                }
            }

            Button {
                text: qsTr("OK")
                onClicked: {
                    storeInitialValues();
                    settingsWindow.visible = false;
                }
            }

        }

    }

}
