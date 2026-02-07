import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import Quester 1.0

Item {
    id: root
    property var magnitudes: AudioVisualizer.magnitudes
    property var barColors: AudioVisualizer.barColors
    property string albumArt: ""
    property bool useProjectM: false
    property color fallbackColor: "white"
    property real barOpacity: 1.0

    signal clicked()

    onWidthChanged: if (width > 0) AudioVisualizer.width = width
    onHeightChanged: if (height > 0) AudioVisualizer.height = height
    Component.onCompleted: {
        if (width > 0) AudioVisualizer.width = width
        if (height > 0) AudioVisualizer.height = height
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

    Row {
        anchors.bottom: parent.bottom
        spacing: 2
        Repeater {
            model: root.magnitudes
            visible: !root.useProjectM
            Rectangle {
                width: 2
                height: root.height * (modelData || 0) * 0.6
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 100
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : root.fallbackColor
                opacity: root.barOpacity
                radius: 1
            }
        }
    }


    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}