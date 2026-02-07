import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: root
    property var magnitudes: []
    property var barColors: AudioVisualizer.barColors
    property string albumArt: ""
    property real contentBottomMargin: 0
    property bool active: false
    property real barOpacity: 0.9
    signal clicked()
    Image {
        id: bg
        anchors.fill: parent
        source: root.albumArt
        visible: true
        z: 1
        fillMode: Image.PreserveAspectStretch
    }
    Image {
        id: bgSource
        anchors.fill: parent
        source: root.albumArt
        visible: false
        fillMode: Image.PreserveAspectCrop
    }

    MultiEffect {
        anchors.fill: parent
        source: bgSource
        z: -1
        blurEnabled: true
        blurMax: 64
        blur: 1.0
        saturation: 0.8
    }

    Row {
        spacing: 2
        Repeater {
            model: root.magnitudes
            Rectangle {
                width: 4
                height: root.height * (modelData || 0) * 0.6
                color: root.barColors && root.barColors.length > index ? root.barColors[index] : "white"
                opacity: root.barOpacity
                radius: 2
                anchors.bottom: parent.bottom
            }
        }
    }


    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}