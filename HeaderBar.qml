import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 50
    color: palette.window
    SystemPalette { id: palette }

    property alias title: titleLabel.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        Label {
            id: titleLabel
            text: "Quester"
            color: palette.windowText
            font.bold: true
            font.pixelSize: 16
            Layout.fillWidth: true
        }
    }
}