import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 50
    color: "#2b2b2b"

    property alias title: titleLabel.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        Label {
            id: titleLabel
            text: "Quester"
            color: "white"
            font.bold: true
            font.pixelSize: 16
            Layout.fillWidth: true
        }
    }
}