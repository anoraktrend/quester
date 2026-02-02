import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    implicitHeight: 50
    property bool transparentBackground: false
    gradient: Gradient {
        GradientStop { position: 0.0; color: root.transparentBackground ? "#00000000" : palette.window}
        GradientStop { position: 1.0; color: "#00000000" }
    }

    SystemPalette { id: palette }

    property alias title: titleLabel.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        Label {
            id: titleLabel
            text: qsTr("Quester")
            color: palette.windowText
            font.bold: true
            font.pixelSize: 16
            Layout.fillWidth: true
        }
    }
}