import QtQuick
import QtQuick.Layouts

Item {
    property var magnitudes: []
    property string albumArt: ""
    property real contentBottomMargin: 0
    SystemPalette { id: palette }

    Image {
        anchors.fill: parent
        source: albumArt
        fillMode: Image.PreserveAspectCrop
    }
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.5
    }

    RowLayout {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20 + contentBottomMargin
        height: parent.height * 0.5
        spacing: 8
        
        Repeater {
            model: 32
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: (magnitudes[index] !== undefined ? magnitudes[index] : 0) * parent.height
                opacity: 0.8
                radius: 4
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 50 }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
    }
}