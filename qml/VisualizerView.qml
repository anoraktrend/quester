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
    property color fallbackColor: palette.text
    property real barOpacity: 1
    property alias settings: visualizerSettings

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

        property int visualizerMode: 0
        property int visualizerStyle: 0 // 0: Bars, 1: Wave Circle
        property int visualizerBarSize: 20
        property int visualizerBarGap: 2
        property real visualizerBarOpacity: 1.0

        category: "Quester"

        onVisualizerBarSizeChanged: AudioVisualizer.visualizerBarSize = visualizerBarSize
        onVisualizerBarGapChanged: AudioVisualizer.visualizerBarGap = visualizerBarGap
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
            var radius = Math.min(width, height) / 4;
            var numMagnitudes = root.magnitudes.length;

            if (numMagnitudes === 0) return;

            ctx.beginPath();
            ctx.lineWidth = 2;
            
            var waveColor = root.barColors && root.barColors.length > 0 ? root.barColors[0] : root.fallbackColor;
            ctx.strokeStyle = waveColor;

            for (var i = 0; i < numMagnitudes; i++) {
                var angle = (i / numMagnitudes) * 2 * Math.PI;
                var magnitude = root.magnitudes[i] * radius * 0.8;

                var x = centerX + Math.cos(angle) * (radius + magnitude);
                var y = centerY + Math.sin(angle) * (radius + magnitude);

                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.closePath();
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
                anchors.bottomMargin: visualizerSettings.visualizerMode === 0 ? 100 : 0
                anchors.topMargin: visualizerSettings.visualizerMode === 1 ? 100 : 0
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: root.barOpacity * visualizerSettings.visualizerBarOpacity
                radius: 1
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }

}
