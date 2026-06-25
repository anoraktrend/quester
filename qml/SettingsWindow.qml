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
            "mpdHost": mpdClient.mpdHost,
            "mpdPort": mpdClient.mpdPort,
            "listenBrainzUsername": mpdClient.listenBrainzUsername,
            "listenBrainzToken": mpdClient.listenBrainzToken,
            // Visualizer settings
            "visualizerMode": visualizerView.settings.visualizerMode,
            "visualizerStyle": visualizerView.settings.visualizerStyle,
            "visualizerBarSize": visualizerView.settings.visualizerBarSize,
            "visualizerBarGap": visualizerView.settings.visualizerBarGap,
            "visualizerBarOpacity": visualizerView.settings.visualizerBarOpacity,
            "visualizerGradient": visualizerView.settings.visualizerGradient,
            // (projectM settings removed)
        };
    }

    function restoreInitialValues() {
        // General settings
        mpdClient.sortMode = initialValues.sortMode;
        mpdClient.consume = initialValues.consume;
        mpdClient.mpdHost = initialValues.mpdHost;
        mpdClient.mpdPort = initialValues.mpdPort;
        mpdClient.listenBrainzUsername = initialValues.listenBrainzUsername;
        mpdClient.listenBrainzToken = initialValues.listenBrainzToken;
        // Visualizer settings
        visualizerView.settings.visualizerMode = initialValues.visualizerMode;
        visualizerView.settings.visualizerStyle = initialValues.visualizerStyle;
        visualizerView.settings.visualizerBarSize = initialValues.visualizerBarSize;
        visualizerView.settings.visualizerBarGap = initialValues.visualizerBarGap;
        visualizerView.settings.visualizerBarOpacity = initialValues.visualizerBarOpacity;
        visualizerView.settings.visualizerGradient = initialValues.visualizerGradient;
        // (projectM settings removed)
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
                    text: qsTr("MPD / Mopidy Connection")
                }

                TextField {
                    id: mpdHostField
                    Kirigami.FormData.label: qsTr("Host:")
                    text: mpdClient.mpdHost
                    onEditingFinished: mpdClient.mpdHost = text
                }

                SpinBox {
                    id: mpdPortField
                    Kirigami.FormData.label: qsTr("Port:")
                    from: 1
                    to: 65535
                    editable: true
                    value: mpdClient.mpdPort
                    onValueModified: mpdClient.mpdPort = value
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    level: 4
                    text: qsTr("Sort Library By:")
                }

                RadioButton {
                    text: qsTr("Artist")
                    checked: mpdClient.sortMode === MpdClient.Artist
                    onClicked: mpdClient.sortMode = MpdClient.Artist
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
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Kirigami.FormLayout {
                    width: parent.width

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
                        model: [qsTr("Bars"), qsTr("Wave Circle")]
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
