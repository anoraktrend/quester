import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quester 1.0
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: root

    property int timePeriod: 0 // 0: Weekly, 1: Monthly, 2: Yearly, 3: All Time
    property var statistics
    property var stats: {
        if (timePeriod === 0)
            return mpdClient.weeklyStats;

        if (timePeriod === 1)
            return mpdClient.monthlyStats;

        if (timePeriod === 2)
            return mpdClient.yearlyStats;

        return mpdClient.allTimeStats;
    }
    property string wrappedImagePath: ""
    property bool generatingWrapped: false

    title: {
        if (timePeriod === 0)
            return qsTr("Your Week in Music");

        if (timePeriod === 1)
            return qsTr("Your Month in Music");

        if (timePeriod === 2)
            return qsTr("Your Year in Music");

        return qsTr("Your All Time Music");
    }

    Item {
        width: parent.width
        implicitHeight: mainLayout.implicitHeight

        ColumnLayout {
            id: mainLayout

            width: Math.min(600, root.width - 40)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.mediumSpacing

                Button {
                    text: qsTr("Week")
                    highlighted: timePeriod === 0
                    onClicked: root.timePeriod = 0
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Month")
                    highlighted: timePeriod === 1
                    onClicked: root.timePeriod = 1
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Year")
                    highlighted: timePeriod === 2
                    onClicked: root.timePeriod = 2
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("All")
                    highlighted: timePeriod === 3
                    onClicked: root.timePeriod = 3
                    Layout.fillWidth: true
                }

            }

            Kirigami.Card {
                Layout.fillWidth: true

                contentItem: ColumnLayout {
                    Kirigami.Heading {
                        level: 1
                        text: qsTr("Total Playtime")
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Kirigami.Heading {
                        level: 2
                        text: {
                            var ms = root.stats.totalMs || 0;
                            var mins = Math.floor(ms / 60000);
                            var hrs = (mins / 60).toFixed(1);
                            return hrs + qsTr(" Hours");
                        }
                        Layout.alignment: Qt.AlignHCenter
                    }

                }

            }

            Kirigami.Card {
                Layout.fillWidth: true

                contentItem: ColumnLayout {
                    Kirigami.Heading {
                        level: 1 
                        text: qsTr("Total Plays")
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Kirigami.Heading {
                        level: 2
                        text: root.stats.totalPlays || 0
                        Layout.alignment: Qt.AlignHCenter
                    }

                }

            }

            Button {
                text: generatingWrapped ? qsTr("Generating...") : qsTr("Generate Wrapped Image")
                enabled: !generatingWrapped
                Layout.fillWidth: true
                onClicked: {
                    generatingWrapped = true;
                    var periodStr = ["weekly", "monthly", "yearly", "all"][timePeriod];
                    statistics.fetchExternalActivityData(periodStr);
                    Qt.callLater(function() {
                        wrappedImagePath = statistics.generateWrappedImage(periodStr);
                        generatingWrapped = false;
                    });
                }
            }

            Kirigami.InlineMessage {
                visible: wrappedImagePath !== ""
                text: qsTr("Saved to: ") + wrappedImagePath
                type: Kirigami.MessageType.Positive
            }

            Kirigami.Heading {
                level: 2
                text: qsTr("External Data")
                visible: statistics.externalActivityData && Object.keys(statistics.externalActivityData).length > 0
            }

            Kirigami.Heading {
                level: 3
                text: qsTr("From ListenBrainz")
                visible: !!(statistics.externalActivityData && statistics.externalActivityData.listenbrainz && statistics.externalActivityData.listenbrainz.lb_top_artists)
            }

            Repeater {
                model: statistics.externalActivityData && statistics.externalActivityData.listenbrainz ? statistics.externalActivityData.listenbrainz.lb_top_artists : []

                delegate: ItemDelegate {
                    width: parent.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            level: 4
                            text: (index + 1) + "."
                            Layout.alignment: Qt.AlignTop
                        }

                        ColumnLayout {
                            Kirigami.Heading {
                                level: 4
                                text: modelData.name || ""
                                font.bold: true
                            }

                            Text {
                                text: (modelData.listen_count || 0) + " listens"
                                font.pixelSize: 12
                                color: Kirigami.Theme.disabledTextColor
                            }

                        }

                    }

                }

            }

            Kirigami.Heading {
                level: 3
                text: qsTr("From Last.fm")
                Layout.topMargin: Kirigami.Units.mediumSpacing
                visible: !!(statistics.externalActivityData && statistics.externalActivityData.lastfm && statistics.externalActivityData.lastfm.top_tracks)
            }

            Repeater {
                model: statistics.externalActivityData && statistics.externalActivityData.lastfm ? statistics.externalActivityData.lastfm.top_tracks : []

                delegate: ItemDelegate {
                    width: parent.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            level: 4
                            text: (index + 1) + "."
                            Layout.alignment: Qt.AlignTop
                        }

                        ColumnLayout {
                            Layout.fillWidth: true

                            Text {
                                text: modelData.title || ""
                                font.bold: true
                                color: Kirigami.Theme.textColor
                            }

                            Text {
                                text: modelData.artist || ""
                                font.pixelSize: 12
                                color: Kirigami.Theme.disabledTextColor
                            }

                        }

                        Text {
                            text: (modelData.play_count || 0) + " plays"
                            color: Kirigami.Theme.textColor
                        }

                    }

                }

            }

            Kirigami.Heading {
                level: 2
                text: qsTr("Top Artists")
            }

            Repeater {
                model: root.stats.topArtists || []

                delegate: ItemDelegate {
                    width: parent.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            level: 4
                            text: (index + 1) + "."
                        }

                        Text {
                            text: modelData.name
                            Layout.fillWidth: true
                            font.bold: true
                            color: Kirigami.Theme.textColor
                        }

                        Text {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: Kirigami.Theme.textColor
                        }

                    }

                }

            }

            Kirigami.Heading {
                level: 2
                text: qsTr("Top Tracks")
                Layout.topMargin: Kirigami.Units.mediumSpacing
            }

            Repeater {
                model: root.stats.topTracks || []

                delegate: ItemDelegate {
                    width: parent.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            level: 4
                            text: (index + 1) + "."
                            Layout.alignment: Qt.AlignTop
                        }

                        ColumnLayout {
                            Layout.fillWidth: true

                            Text {
                                text: modelData.title
                                font.bold: true
                                color: Kirigami.Theme.textColor
                            }

                            Text {
                                text: modelData.artist
                                font.pixelSize: 12
                                color: Kirigami.Theme.disabledTextColor
                            }

                        }

                        Text {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: Kirigami.Theme.textColor
                        }

                    }

                }

            }

            Kirigami.Heading {
                level: 2
                text: qsTr("Top Albums")
                Layout.topMargin: Kirigami.Units.mediumSpacing
            }

            Repeater {
                model: root.stats.topAlbums || []

                delegate: ItemDelegate {
                    width: parent.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.mediumSpacing

                        Kirigami.Heading {
                            level: 4
                            text: (index + 1) + "."
                        }

                        Text {
                            text: modelData.name
                            Layout.fillWidth: true
                            font.bold: true
                            color: Kirigami.Theme.textColor
                        }

                        Text {
                            text: Math.floor(modelData.ms / 60000) + "m"
                            color: Kirigami.Theme.textColor
                        }

                    }

                }

            }

        }

    }

}
