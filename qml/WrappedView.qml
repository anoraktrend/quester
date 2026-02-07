import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Quester 1.0

Item {
    id: root
    property int timePeriod: 0 // 0: Weekly, 1: Monthly, 2: Yearly, 3: All Time

    property var stats: {
        if (timePeriod === 0) return mpdClient.weeklyStats
        if (timePeriod === 1) return mpdClient.monthlyStats
        if (timePeriod === 2) return mpdClient.yearlyStats
        return mpdClient.allTimeStats
    }

    SystemPalette { id: palette }

    Rectangle {
        anchors.fill: parent
        color: palette.window
    }

    Flickable {
        anchors.fill: parent
        contentHeight: contentLayout.height + 40
        clip: true

        ColumnLayout {
            id: contentLayout
            width: Math.min(600, parent.width - 40)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 20
            spacing: 20

            Label {
                text: {
                    if (timePeriod === 0) return qsTr("Your Week in Music")
                    if (timePeriod === 1) return qsTr("Your Month in Music")
                    if (timePeriod === 2) return qsTr("Your Year in Music")
                    return qsTr("Your All Time Music")
                }
                font.pixelSize: 32
                font.bold: true
                color: palette.text
                Layout.alignment: Qt.AlignHCenter
            }

            // Time Period Selector
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Button {
                    text: qsTr("Week")
                    checked: timePeriod === 0
                    onClicked: root.timePeriod = 0
                    Layout.fillWidth: true
                    background: Rectangle {
                        color: parent.checked ? palette.highlight : palette.base
                        radius: 5
                    }
                }

                Button {
                    text: qsTr("Month")
                    checked: timePeriod === 1
                    onClicked: root.timePeriod = 1
                    Layout.fillWidth: true
                    background: Rectangle {
                        color: parent.checked ? palette.highlight : palette.base
                        radius: 5
                    }
                }

                Button {
                    text: qsTr("Year")
                    checked: timePeriod === 2
                    onClicked: root.timePeriod = 2
                    Layout.fillWidth: true
                    background: Rectangle {
                        color: parent.checked ? palette.highlight : palette.base
                        radius: 5
                    }
                }

                Button {
                    text: qsTr("All")
                    checked: timePeriod === 3
                    onClicked: root.timePeriod = 3
                    Layout.fillWidth: true
                    background: Rectangle {
                        color: parent.checked ? palette.highlight : palette.base
                        radius: 5
                    }
                }
            }

            // Total Time Card
            Rectangle {
                Layout.fillWidth: true
                height: 100
                color: palette.highlight
                radius: 10

                ColumnLayout {
                    anchors.centerIn: parent
                    Label {
                        text: qsTr("Total Playtime")
                        color: palette.highlightedText
                        font.pixelSize: 14
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: {
                            var ms = root.stats.totalMs || 0
                            var mins = Math.floor(ms / 60000)
                            var hrs = (mins / 60).toFixed(1)
                            return hrs + qsTr(" Hours")
                        }
                        color: palette.highlightedText
                        font.pixelSize: 36
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Total Plays Card
            Rectangle {
                Layout.fillWidth: true
                height: 100
                color: palette.highlight
                radius: 10

                ColumnLayout {
                    anchors.centerIn: parent
                    Label {
                        text: qsTr("Total Plays")
                        color: palette.highlightedText
                        font.pixelSize: 14
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: root.stats.totalPlays || 0
                        color: palette.highlightedText
                        font.pixelSize: 36
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Top Artists
            Label {
                text: qsTr("Top Artists")
                font.pixelSize: 22
                font.bold: true
                color: palette.text
            }

            Repeater {
                model: root.stats.topArtists || []
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: palette.base
                    radius: 5
                    border.color: palette.mid

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        Label {
                            text: (index + 1) + "."
                            font.bold: true
                            color: palette.highlight
                        }
                        Label {
                            text: modelData.name
                            Layout.fillWidth: true
                            color: palette.text
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        Label {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: palette.windowText
                        }
                    }
                }
            }

            // Top Tracks
            Label {
                text: qsTr("Top Tracks")
                font.pixelSize: 22
                font.bold: true
                color: palette.text
                Layout.topMargin: 10
            }

            Repeater {
                model: root.stats.topTracks || []
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: palette.base
                    radius: 5
                    border.color: palette.mid

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        Label {
                            text: (index + 1) + "."
                            font.bold: true
                            color: palette.highlight
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.title; color: palette.text; font.pixelSize: 16; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.artist; color: palette.windowText; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        Label {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: palette.windowText
                        }
                    }
                }
            }

            // Top Albums
            Label {
                text: qsTr("Top Albums")
                font.pixelSize: 22
                font.bold: true
                color: palette.text
                Layout.topMargin: 10
            }

            Repeater {
                model: root.stats.topAlbums || []
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: palette.base
                    radius: 5
                    border.color: palette.mid

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        Label {
                            text: (index + 1) + "."
                            font.bold: true
                            color: palette.highlight
                        }
                        Label {
                            text: modelData.name
                            Layout.fillWidth: true
                            color: palette.text
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        Label {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: palette.windowText
                        }
                    }
                }
            }
        }
    }
}