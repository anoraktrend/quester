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
            "visualizerStyle": visualizerView.settings.visualizerStyle,
            "visualizerBarSize": visualizerView.settings.visualizerBarSize,
            "visualizerBarGap": visualizerView.settings.visualizerBarGap,
            "visualizerBarOpacity": visualizerView.settings.visualizerBarOpacity,
            "visualizerGradient": visualizerView.settings.visualizerGradient,
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
        visualizerView.settings.visualizerStyle = initialValues.visualizerStyle;
        visualizerView.settings.visualizerBarSize = initialValues.visualizerBarSize;
        visualizerView.settings.visualizerBarGap = initialValues.visualizerBarGap;
        visualizerView.settings.visualizerBarOpacity = initialValues.visualizerBarOpacity;
        visualizerView.settings.visualizerGradient = initialValues.visualizerGradient;
        // projectM settings
        visualizerView.settings.projectMPresetPath        = initialValues.projectMPresetPath;
        visualizerView.settings.projectMBeatSensitivity   = initialValues.projectMBeatSensitivity;
        visualizerView.settings.projectMPresetDuration    = initialValues.projectMPresetDuration;
        visualizerView.settings.projectMSoftCutDuration   = initialValues.projectMSoftCutDuration ?? 3.0;
        visualizerView.settings.projectMMeshX             = initialValues.projectMMeshX;
        visualizerView.settings.projectMMeshY             = initialValues.projectMMeshY;
        visualizerView.settings.projectMFPS               = initialValues.projectMFPS;
        visualizerView.settings.projectMShuffleEnabled    = initialValues.projectMShuffleEnabled;
        visualizerView.settings.projectMShowBars          = initialValues.projectMShowBars;
        visualizerView.settings.projectMBarOpacity        = initialValues.projectMBarOpacity;
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
                    text: qsTr("Style:")
                }

                ComboBox {
                    model: [qsTr("Bars"), qsTr("Wave Circle"), qsTr("Milkdrop (projectM)")]
                    currentIndex: visualizerView.settings.visualizerStyle
                    onActivated: visualizerView.settings.visualizerStyle = currentIndex
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

                // ── projectM / Milkdrop Settings ──────────────────────────────
                // Shown only when style 2 (Milkdrop) is selected.
                Kirigami.Separator {
                    Layout.fillWidth: true
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                Kirigami.Heading {
                    level: 4
                    text: qsTr("Milkdrop (projectM) Settings")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                Label {
                    text: qsTr("Preset Folder:")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                RowLayout {
                    visible: visualizerView.settings.visualizerStyle === 2
                    Layout.fillWidth: true
                    TextField {
                        id: presetPathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("/usr/share/projectM/presets")
                        text: visualizerView.settings.projectMPresetPath
                        onEditingFinished: visualizerView.settings.projectMPresetPath = text
                    }
                }

                Label {
                    text: qsTr("Beat Sensitivity:")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                Slider {
                    id: beatSensSlider
                    visible: visualizerView.settings.visualizerStyle === 2
                    from: 0.0
                    to: 5.0
                    stepSize: 0.05
                    value: visualizerView.settings.projectMBeatSensitivity
                    onValueChanged: visualizerView.settings.projectMBeatSensitivity = value
                    Label {
                        anchors { left: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 4 }
                        text: beatSensSlider.value.toFixed(2)
                    }
                }

                Label {
                    text: qsTr("Preset Duration (s):")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                SpinBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    from: 1
                    to: 300
                    value: visualizerView.settings.projectMPresetDuration
                    onValueChanged: visualizerView.settings.projectMPresetDuration = value
                }

                Label {
                    text: qsTr("Soft-cut Duration (s):")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                SpinBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    from: 1
                    to: 30
                    value: visualizerView.settings.projectMSoftCutDuration
                    onValueChanged: visualizerView.settings.projectMSoftCutDuration = value
                }

                Label {
                    text: qsTr("Target FPS:")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                SpinBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    from: 15
                    to: 144
                    value: visualizerView.settings.projectMFPS
                    onValueChanged: visualizerView.settings.projectMFPS = value
                }

                Label {
                    text: qsTr("Mesh Size:")
                    visible: visualizerView.settings.visualizerStyle === 2
                }

                RowLayout {
                    visible: visualizerView.settings.visualizerStyle === 2
                    Label { text: qsTr("W:") }
                    SpinBox {
                        from: 4; to: 256
                        value: visualizerView.settings.projectMMeshX
                        onValueChanged: visualizerView.settings.projectMMeshX = value
                    }
                    Label { text: qsTr("H:") }
                    SpinBox {
                        from: 4; to: 256
                        value: visualizerView.settings.projectMMeshY
                        onValueChanged: visualizerView.settings.projectMMeshY = value
                    }
                }

                CheckBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    text: qsTr("Shuffle Presets")
                    checked: visualizerView.settings.projectMShuffleEnabled
                    onToggled: visualizerView.settings.projectMShuffleEnabled = checked
                }

                CheckBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    text: qsTr("Lock Current Preset")
                    checked: visualizerView.settings.projectMPresetLocked
                    onToggled: visualizerView.settings.projectMPresetLocked = checked
                }

                CheckBox {
                    visible: visualizerView.settings.visualizerStyle === 2
                    text: qsTr("Overlay Spectrum Bars")
                    checked: visualizerView.settings.projectMShowBars
                    onToggled: visualizerView.settings.projectMShowBars = checked
                }

                Label {
                    text: qsTr("Overlay Bar Opacity:")
                    visible: visualizerView.settings.visualizerStyle === 2 && visualizerView.settings.projectMShowBars
                }

                Slider {
                    visible: visualizerView.settings.visualizerStyle === 2 && visualizerView.settings.projectMShowBars
                    from: 0.05
                    to: 1.0
                    stepSize: 0.05
                    value: visualizerView.settings.projectMBarOpacity
                    onValueChanged: visualizerView.settings.projectMBarOpacity = value
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
