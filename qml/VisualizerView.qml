import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import Quester 1.0
import org.kde.kirigami as Kirigami
// ProjectMVisualizer is only registered when built with HAVE_PROJECTM.
// Guard all ProjectM usage with visualizerSettings.visualizerStyle === 2.

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
    property color fallbackColor: palette.text
    property real barOpacity: 1
    property alias settings: visualizerSettings

    property var gradientColors: ({})

    signal clicked()

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
        
        // Pass settings to C++ backend
        AudioVisualizer.visualizerBarSize = visualizerSettings.visualizerBarSize;
        AudioVisualizer.visualizerBarGap = visualizerSettings.visualizerBarGap;

        var presets = JSON.parse(AudioVisualizer.loadVisualizerGradients());
        var tempColors = {};
        for (var key in presets) {
            if (presets.hasOwnProperty(key)) {
                var value = presets[key];
                if (Array.isArray(value)) {
                    tempColors[key] = value;
                } else if (typeof value === 'object' && value !== null && Array.isArray(value.colors)) {
                    tempColors[key] = value.colors;
                }
            }
        }
        gradientColors = tempColors;
    }
    Component.onDestruction: {
        if (_resizeTimer) {
            _resizeTimer.stop();
            _resizeTimer.destroy();
        }
    }
    onVisibleChanged: {
        if (visible) {
            AudioVisualizer.start();
        } else {
            AudioVisualizer.stop();
        }
        if (projectMLoader.item)
            projectMLoader.item.active = visible && (visualizerSettings.visualizerStyle === 2);
    }

    Settings {
        id: visualizerSettings

        property int  visualizerMode:       0
        property int  visualizerStyle:      0 // 0: Bars, 1: Wave Circle, 2: Milkdrop (projectM)
        property int  visualizerBarSize:    20
        property int  visualizerBarGap:     2
        property real visualizerBarOpacity: 1.0
        property string visualizerGradient: "rainbow"

        // ── projectM settings (persisted via QSettings) ──────────────────
        // These are written through to the C++ ProjectMItem via property binding.
        property string projectMPresetPath:        ""
        property double projectMBeatSensitivity:   1.0
        property double projectMSoftCutDuration:   3.0
        property double projectMPresetDuration:    30.0
        property bool   projectMHardCutEnabled:    false
        property double projectMHardCutSensitivity: 0.1
        property int    projectMMeshX:             32
        property int    projectMMeshY:             24
        property bool   projectMAspectCorrection:  true
        property int    projectMFPS:               60
        property bool   projectMShuffleEnabled:    true
        property bool   projectMPresetLocked:      false
        property int    projectMSelectedPreset:    0
        // Overlay bars on top of projectM render
        property bool   projectMShowBars:          false
        property real   projectMBarOpacity:        0.5

        category: "Quester"

        onVisualizerBarSizeChanged: AudioVisualizer.visualizerBarSize = visualizerBarSize
        onVisualizerBarGapChanged:  AudioVisualizer.visualizerBarGap  = visualizerBarGap
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
    }

    MultiEffect {
        anchors.fill: parent
        source: bg
        brightness: -0.3
    }

    Canvas {
        id: circleCanvas
        anchors.fill: parent
        visible: visualizerSettings.visualizerStyle === 1
        z: 6

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var centerX = width / 2;
            var centerY = height / 2;
            var baseRadius = Math.min(width, height) / 4;
            var numMagnitudes = root.magnitudes.length;
            var colors = root.barColors;

            if (numMagnitudes === 0 || colors.length === 0) return;

            // Use barColors from AudioVisualizer (interpolated preset colors)
            var presetColors = colors;

            // Calculate the maximum extent for the gradient based on magnitudes
            var maxMagnitude = 0;
            for (var m = 0; m < numMagnitudes; m++) {
                if (root.magnitudes[m] > maxMagnitude) {
                    maxMagnitude = root.magnitudes[m];
                }
            }
            var maxRadius = baseRadius + (maxMagnitude * baseRadius * 0.8);

            // Create gradient for fill - from center outward using preset colors
            var fillGradient = ctx.createRadialGradient(centerX, centerY, 0, centerX, centerY, maxRadius);
            for (var j = 0; j < presetColors.length; j++) {
                var color = presetColors[j];
                fillGradient.addColorStop(j / (presetColors.length - 1), color);
            }

            // Create gradient for stroke - along the circle path
            var strokeGradient = ctx.createRadialGradient(centerX, centerY, baseRadius, centerX, centerY, maxRadius);
            for (var k = 0; k < presetColors.length; k++) {
                var strokeColor = presetColors[k];
                strokeGradient.addColorStop(k / (presetColors.length - 1), strokeColor);
            }

            ctx.beginPath();
            ctx.lineWidth = 3;
            ctx.fillStyle = fillGradient;
            ctx.strokeStyle = strokeGradient;

            // Draw the circular visualizer path
            for (var i = 0; i < numMagnitudes; i++) {
                var angle = (i / numMagnitudes) * 2 * Math.PI;
                var magnitude = root.magnitudes[i] * baseRadius * 0.8;

                var x = centerX + Math.cos(angle) * (baseRadius + magnitude);
                var y = centerY + Math.sin(angle) * (baseRadius + magnitude);

                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.closePath();
            
            // Fill the shape with gradient
            ctx.fill();
            // Stroke the shape with gradient
            ctx.stroke();
        }

        Connections {
            target: AudioVisualizer
            function onMagnitudesChanged() {
                circleCanvas.requestPaint();
            }
        }
    }

    Item {
        anchors.fill: parent
        z: 6
        visible: visualizerSettings.visualizerStyle === 0

        Repeater {
            model: root.magnitudes

            Rectangle {
                width: visualizerSettings.visualizerBarSize
                height: root.height * (modelData || 0) * 0.6
                x: index * (visualizerSettings.visualizerBarSize + visualizerSettings.visualizerBarGap)
                anchors.bottom: visualizerSettings.visualizerMode === 0 ? parent.bottom : undefined
                anchors.top: visualizerSettings.visualizerMode === 1 ? parent.top : undefined
                anchors.verticalCenter: visualizerSettings.visualizerMode === 2 ? parent.verticalCenter : undefined
                anchors.bottomMargin: 0
                anchors.topMargin: 0
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: root.barOpacity * visualizerSettings.visualizerBarOpacity
                radius: 1
            }
        }
    }

    // ── projectM / Milkdrop style (style 2) ──────────────────────────────
    // Loaded lazily so it does not pull in OpenGL state when not used.
    Loader {
        id: projectMLoader
        anchors.fill: parent
        z: 5
        active: visualizerSettings.visualizerStyle === 2
        visible: active

        sourceComponent: Component {
            // ProjectMVisualizer is a QQuickFramebufferObject registered in main.cpp
            // only when HAVE_PROJECTM is defined. If the type is unknown this Loader
            // simply fails silently (the error is caught in onStatusChanged below).
            ProjectMVisualizer {
                id: pmViz
                anchors.fill: parent

                // ── Bind settings from QML → C++ ──
                presetPath:         visualizerSettings.projectMPresetPath
                beatSensitivity:    visualizerSettings.projectMBeatSensitivity
                softCutDuration:    visualizerSettings.projectMSoftCutDuration
                presetDuration:     visualizerSettings.projectMPresetDuration
                hardCutEnabled:     visualizerSettings.projectMHardCutEnabled
                hardCutSensitivity: visualizerSettings.projectMHardCutSensitivity
                meshX:              visualizerSettings.projectMMeshX
                meshY:              visualizerSettings.projectMMeshY
                aspectCorrection:   visualizerSettings.projectMAspectCorrection
                targetFps:          visualizerSettings.projectMFPS
                shuffleEnabled:     visualizerSettings.projectMShuffleEnabled
                presetLocked:       visualizerSettings.projectMPresetLocked
                active:             root.visible && (visualizerSettings.visualizerStyle === 2)

                // ── Sync selected preset back to QML ──
                onCurrentPresetIndexChanged: {
                    visualizerSettings.projectMSelectedPreset = currentPresetIndex;
                }

                // ── Wire up audio (one-shot: AudioVisualizer is a context property) ──
                Component.onCompleted: {
                    setAudioVisualizerSource(AudioVisualizer);
                    // Restore saved preset selection
                    if (visualizerSettings.projectMSelectedPreset >= 0)
                        currentPresetIndex = visualizerSettings.projectMSelectedPreset;
                }
            }
        }

        onStatusChanged: {
            if (status === Loader.Error)
                console.warn("[VisualizerView] ProjectMVisualizer not available (built without HAVE_PROJECTM?)");
        }
    }

    // ── Optional bars overlay on top of projectM ─────────────────────────
    Item {
        anchors.fill: parent
        z: 7
        visible: visualizerSettings.visualizerStyle === 2 && visualizerSettings.projectMShowBars

        Repeater {
            model: root.magnitudes

            Rectangle {
                width: visualizerSettings.visualizerBarSize
                height: root.height * (modelData || 0) * 0.6
                x: index * (visualizerSettings.visualizerBarSize + visualizerSettings.visualizerBarGap)
                anchors.bottom: parent.bottom
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: visualizerSettings.projectMBarOpacity
                radius: 1
            }
        }
    }

    // ── Preset navigation overlay (shown in projectM mode) ───────────────
    Row {
        id: presetNav
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 12
        }
        z: 10
        spacing: 8
        visible: visualizerSettings.visualizerStyle === 2

        RoundButton {
            text: "◀"
            onClicked: if (projectMLoader.item) projectMLoader.item.previousPreset()
        }
        Label {
            text: (projectMLoader.item && projectMLoader.item.presetNames.length > 0)
                  ? projectMLoader.item.presetNames[projectMLoader.item.currentPresetIndex] ?? ""
                  : ""
            color: "white"
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 11
        }
        RoundButton {
            text: "▶"
            onClicked: if (projectMLoader.item) projectMLoader.item.nextPreset()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }

}
