import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import Quester 1.0
import org.kde.kirigami as Kirigami

Item {
    id: root

    // Viewport size constraints
    property int minViewportWidth: 100
    property int minViewportHeight: 100
    // Debounce timer for resize events (ms)
    property int resizeDebounceDelay: 100
    
    // Internal state
    property int _pendingWidth: -1
    property int _pendingHeight: -1
    property Timer _resizeTimer

    property var magnitudes: AudioVisualizer.magnitudes
    property var barColors: AudioVisualizer.barColors
    property string albumArt: ""
    property bool useProjectM: false
    property color fallbackColor: palette.text
    property real barOpacity: 1
    property alias settings: visualizerSettings
    property var presetModel: []

    signal clicked()

    function loadPresetList() {
        var presetPath = visualizerSettings.projectMPresetPath;
        if (!presetPath)
            presetPath = "/usr/share/projectM/presets";

        var presets = projectM.getPresetList(presetPath);
        root.presetModel = presets.length > 0 ? presets : ["Preset 1", "Preset 2", "Preset 3"];
    }

    // Debounced viewport size update
    function _updateViewportSize() {
        // Apply pending size if valid
        if (_pendingWidth > 0 && _pendingHeight > 0) {
            var finalWidth = Math.max(minViewportWidth, _pendingWidth);
            var finalHeight = Math.max(minViewportHeight, _pendingHeight);
            AudioVisualizer.width = finalWidth;
            AudioVisualizer.height = finalHeight;
            _pendingWidth = -1;
            _pendingHeight = -1;
        }
    }

    // Schedule a debounced update
    function _scheduleViewportUpdate(newWidth, newHeight) {
        _pendingWidth = newWidth;
        _pendingHeight = newHeight;
        if (_resizeTimer) {
            _resizeTimer.restart();
        }
    }

    onWidthChanged: {
        if (width >= minViewportWidth) {
            _scheduleViewportUpdate(width, height);
        }
    }
    onHeightChanged: {
        if (height >= minViewportHeight) {
            _scheduleViewportUpdate(width, height);
        }
    }
    Component.onCompleted: {
        // Initialize resize timer
        _resizeTimer = Qt.createQmlObject(
            "import QtQuick 2.15; Timer { interval: resizeDebounceDelay; running: false; repeat: false; onTriggered: root._updateViewportSize() }",
            root, "_resizeTimer"
        );
        
        // Set initial size with constraints
        var finalWidth = Math.max(minViewportWidth, width);
        var finalHeight = Math.max(minViewportHeight, height);
        if (finalWidth > 0)
            AudioVisualizer.width = finalWidth;
        if (finalHeight > 0)
            AudioVisualizer.height = finalHeight;

        AudioVisualizer.updateSystemColors(Kirigami.Theme.highlightColor, Kirigami.Theme.textColor);
        loadPresetList();
    }
    Component.onDestruction: {
        if (_resizeTimer) {
            _resizeTimer.stop();
            _resizeTimer.destroy();
        }
    }
    onVisibleChanged: {
        if (visible)
            AudioVisualizer.start();
        else
            AudioVisualizer.stop();
    }

    Settings {
        id: visualizerSettings

        property string projectMPresetPath: ""
        property int projectMTextureSize: 2048
        property int projectMMeshX: 64
        property int projectMMeshY: 48
        property int projectMFPS: 60
        property int projectMSmoothPresetDuration: 5
        property int projectMPresetDuration: 15
        property real projectMBeatSensitivity: 10
        property bool projectMShuffleEnabled: true
        property string projectMSelectedPreset: ""
        property int visualizerMode: 0
        property bool projectMShowBars: false
        property real projectMBarOpacity: 0.8
        property int visualizerBarSize: 20
        property int visualizerBarGap: 2
        property real visualizerBarOpacity: 1.0

        category: "Quester"
        onProjectMPresetPathChanged: root.loadPresetList()
        onProjectMSelectedPresetChanged: {
            if (root.useProjectM && projectMSelectedPreset !== "")
                projectM.selectPresetByName(projectMSelectedPreset, true);

        }
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
        blur: 1
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

        // Calculate dynamic bar dimensions based on window width, bar size, and gap
        property real barSlotWidth: {
            var gap = visualizerSettings.visualizerBarGap;
            var targetBarWidth = visualizerSettings.visualizerBarSize;
            var numMagnitudes = root.magnitudes.length || 1;
            
            // If user set a specific bar size, use it but also adjust number of bars
            if (targetBarWidth > 0) {
                // Calculate how many bars can fit: floor(width / (barWidth + gap))
                var slots = Math.floor(root.width / (targetBarWidth + gap));
                // But ensure at least 1 bar and at most the available magnitudes
                slots = Math.max(1, Math.min(slots, numMagnitudes));
                // Calculate actual slot width to fill the window
                return root.width / slots;
            } else {
                // Auto mode: use available magnitudes with gap
                return (root.width / numMagnitudes);
            }
        }

        Repeater {
            model: root.magnitudes
            visible: !root.useProjectM || visualizerSettings.projectMShowBars

            Rectangle {
                width: {
                    var gap = visualizerSettings.visualizerBarGap;
                    var targetBarWidth = visualizerSettings.visualizerBarSize;
                    var slotWidth = parent.barSlotWidth;
                    
                    if (targetBarWidth > 0) {
                        // Fixed bar width mode - bars have user-specified width
                        return Math.max(1, targetBarWidth);
                    } else {
                        // Auto mode - bars fill the space
                        return Math.max(1, slotWidth - gap);
                    }
                }
                height: root.height * (modelData || 0) * 0.6
                visible: !root.useProjectM || visualizerSettings.projectMShowBars
                x: index * parent.barSlotWidth + (visualizerSettings.visualizerBarGap / 2)
                anchors.bottom: visualizerSettings.visualizerMode === 0 ? parent.bottom : undefined
                anchors.top: visualizerSettings.visualizerMode === 1 ? parent.top : undefined
                anchors.verticalCenter: visualizerSettings.visualizerMode === 2 ? parent.verticalCenter : undefined
                anchors.bottomMargin: visualizerSettings.visualizerMode === 0 ? 100 : 0
                anchors.topMargin: visualizerSettings.visualizerMode === 1 ? 100 : 0
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: root.useProjectM 
                    ? visualizerSettings.projectMBarOpacity 
                    : (root.barOpacity * visualizerSettings.visualizerBarOpacity)
                radius: 1
            }

        }

    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }

}