import QtQuick
import QtQuick.Layouts

import QtQuick.Controls.Material 2.15

Item {
    property var magnitudes: []
    property string albumArt: ""

    Image {
        id: backgroundImage
        anchors.fill: parent
        source: albumArt
        fillMode: Image.PreserveAspectCrop
        opacity: 0.3
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.hsla(Material.hue, 0, 0, 0.2) }
            GradientStop { position: 1.0; color: Qt.hsla(Material.hue, 0, 0, 0.8) }
        }
    }
    
    RowLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        height: parent.height * 0.8
        spacing: 8
        
        Repeater {
            model: 32
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: (magnitudes[index] !== undefined ? magnitudes[index] : 0) * parent.height
                color: Qt.hsla(Material.accentHue, 0.6, 0.5 + (index/64.0), 0.8)
                opacity: 0.8
                radius: 4
                
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 50 } }
            }
        }
    }
}