import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    property var magnitudes: []
    property string albumArt: ""
    property real contentBottomMargin: 0
    property bool active: false
    signal clicked()
    clip: true

    Image {
        anchors.fill: parent
        source: "image://blur/" + albumArt
        fillMode: Image.PreserveAspectCrop
        opacity: 0.5
    }
    Image {
        anchors.fill: parent
        source: albumArt
        fillMode: Image.PreserveAspectFit
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#88000000" }
            GradientStop { position: 1.0; color: "black" }
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
                Layout.preferredHeight: Math.max(4, visualizerRow.height * (magnitudes[index] || 0))
                color: AudioVisualizer.barColors[index] || "white"
                opacity: 0.9
                radius: 4
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 80; easing.type: Easing.OutQuad }
                }
            }
        }
    }


    MouseArea {
        anchors.fill: parent
        onClicked: parent.clicked()
    }
}