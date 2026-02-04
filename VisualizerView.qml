import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Pane {
    id: root
    property var magnitudes: []
    property string albumArt: ""
    property real contentBottomMargin: 0
    property bool active: false
    property real barOpacity: 0.9
    signal clicked()
    padding: 0
    clip: true

    background: Item {
        Image {
            anchors.fill: parent
            source: "image://blur/" + root.albumArt
            fillMode: Image.PreserveAspectCrop
            opacity: 0.5
        }
        Image {
            anchors.fill: parent
            source: root.albumArt
            fillMode: Image.PreserveAspectFit
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#88000000" }
                GradientStop { position: 1.0; color: "black" }
            }
        }
    }

    RowLayout {
        id: visualizerRow
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20 + contentBottomMargin
        spacing: 1

        Repeater {
            model: magnitudes.length
            Rectangle {

                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(4, visualizerRow.height * (root.magnitudes[index] || 0))
                color: AudioVisualizer.barColors[index] || "white"
                opacity: root.barOpacity
                radius: 4
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 80; easing.type: Easing.OutQuad }
                }
            }
        }
    }


    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}