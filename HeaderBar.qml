import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    implicitHeight: 50
    property bool transparentBackground: false
    property real fontScale: 1.0
    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.transparentBackground ? palette.dark : palette.mid}
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    SystemPalette { id: palette }

    property alias title: titleLabel.text

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        Image {
            source: "qrc:/Quester.svg"
            sourceSize: Qt.size(32, 32)
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            fillMode: Image.PreserveAspectFit
        }
        Label {
            id: titleLabel
            text: qsTr("Quester")
            color: palette.windowText
            font.bold: true
            font.pixelSize: 16 * root.fontScale
            Layout.fillWidth: true
        }
    }
}