import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import Quester 1.0
import QtCore

Item {
    id: root
    property var magnitudes: AudioVisualizer.magnitudes
    property var barColors: AudioVisualizer.barColors
    property string albumArt: ""
    property bool useProjectM: false
    property color fallbackColor: "white"
    property real barOpacity: 1.0

    property alias settings: visualizerSettings
    property var presetModel: []

    signal clicked()

    SystemPalette { id: palette }

    Settings {
        id: visualizerSettings
        category: "Quester"
        property string projectMPresetPath: ""
        property int projectMTextureSize: 2048
        property int projectMMeshX: 64
        property int projectMMeshY: 48
        property int projectMFPS: 60
        property int projectMSmoothPresetDuration: 5
        property int projectMPresetDuration: 15
        property real projectMBeatSensitivity: 10.0
        property bool projectMShuffleEnabled: true
        property string projectMSelectedPreset: ""
        property int visualizerMode: 0
        property bool projectMShowBars: false
        property real projectMBarOpacity: 0.8

        onProjectMPresetPathChanged: root.loadPresetList()
        onProjectMSelectedPresetChanged: {
            if (root.useProjectM && projectMSelectedPreset !== "") {
                projectM.selectPresetByName(projectMSelectedPreset, true)
            }
        }
    }

    function loadPresetList() {
        var presetPath = visualizerSettings.projectMPresetPath
        if (!presetPath) {
            presetPath = "/usr/share/projectM/presets"
        }
        var presets = projectM.getPresetList(presetPath)
        root.presetModel = presets.length > 0 ? presets : ["Preset 1", "Preset 2", "Preset 3"]
    }

    onWidthChanged: if (width > 0) AudioVisualizer.width = width
    onHeightChanged: if (height > 0) AudioVisualizer.height = height
    Component.onCompleted: {
        if (width > 0) AudioVisualizer.width = width
        if (height > 0) AudioVisualizer.height = height
        AudioVisualizer.updateSystemColors(palette.highlight, palette.text)
        loadPresetList()
    }

    onVisibleChanged: {
        if (visible) AudioVisualizer.start()
        else AudioVisualizer.stop()
    }

    ProjectMVisualizer {
        id: projectM
        anchors.fill: parent
        visible: root.useProjectM && root.visible
        active: visible
        z: 5
        shuffleEnabled: visualizerSettings.projectMShuffleEnabled
    }

    Image {
        id: bg
        anchors.fill: parent
        source: root.albumArt
        visible: false
        z: -1
        fillMode: Image.PreserveAspectFit
    }
    Image {
        id: vizBgSource
        anchors.fill: parent
        source: root.albumArt
        visible: false
        fillMode: Image.PreserveAspectCrop
    }

    MultiEffect {
        anchors.fill: parent
        source: vizBgSource
        blurEnabled: true
        blurMax: 64
        blur: 1.0
        saturation: 0.8
        brightness: -0.5
        visible: !root.useProjectM
    }

    MultiEffect {
        anchors.fill: parent
        source: bg
        brightness: -0.3
        visible: !root.useProjectM
    }

    Item {
        anchors.fill: parent
        z: 6
        Repeater {
            model: root.magnitudes
            visible: !root.useProjectM || visualizerSettings.projectMShowBars
            Rectangle {
                width: 2
                height: root.height * (modelData || 0) * 0.6
                visible: !root.useProjectM || visualizerSettings.projectMShowBars
                x: index * 4
                anchors.bottom: visualizerSettings.visualizerMode === 0 ? parent.bottom : undefined
                anchors.top: visualizerSettings.visualizerMode === 1 ? parent.top : undefined
                anchors.verticalCenter: visualizerSettings.visualizerMode === 2 ? parent.verticalCenter : undefined
                anchors.bottomMargin: visualizerSettings.visualizerMode === 0 ? 100 : 0
                anchors.topMargin: visualizerSettings.visualizerMode === 1 ? 100 : 0
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: root.barOpacity * (root.useProjectM ? visualizerSettings.projectMBarOpacity : 1.0)
                radius: 1
            }
        }
    }


    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}