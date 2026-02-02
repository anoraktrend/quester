import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    property var magnitudes: []
    property string albumArt: ""
    property real contentBottomMargin: 0
    property bool active: false
    signal clicked()

    Image {
        anchors.fill: parent
        source: albumArt
        fillMode: Image.PreserveAspectCrop
    }
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.7
    }

    RowLayout {
        id: visualizerRow
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20 + contentBottomMargin
        height: parent.height
        spacing: 1

        Repeater {
            model: magnitudes.length
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: Math.max(4, (parent.height - 50) * (magnitudes[index] || 0))
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