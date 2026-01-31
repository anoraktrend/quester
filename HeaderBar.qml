import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QtQuick.Controls.Material 2.15

Rectangle {
    id: root
    implicitHeight: 50
    color: Material.primaryColor

    property alias title: titleLabel.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        Label {
            id: titleLabel
            text: "Quester"
            color: Material.primaryTextColor
            font.bold: true
            font.pixelSize: 16
            Layout.fillWidth: true
        }
    }
}