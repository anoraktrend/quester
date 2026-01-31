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
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 20 + contentBottomMargin
        height: parent.height * 0.6
        spacing: 4
        
        Repeater {
            model: 32
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: Math.max(4, (magnitudes && magnitudes[index] !== undefined ? magnitudes[index] : 0) * parent.height)
                color: Qt.hsla(index / 32.0, 0.7, 0.6, 1.0)
                opacity: 0.9
                radius: 4
                
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 80; easing.type: Easing.OutQuad }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        text: qsTr("Visualizer Disconnected\nCheck MPD FIFO Configuration")
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        visible: !active
    }

    MouseArea {
        anchors.fill: parent
        onClicked: parent.clicked()
    }
}