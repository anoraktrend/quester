import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quester 1.0

Item {
    id: gridViewMode
    anchors.fill: parent

    property string viewMode: "albums" // Possible values: "artists", "albums"
    property string currentArtist: ""
    property bool selectionMode: false
    property var selectedAlbums: []

    SystemPalette { id: palette }

    // Filtered album model for artist-focused view
    ListModel {
        id: filteredAlbumModel
    }

    // Artist model
    ListModel {
        id: artistModel
    }

    Component.onCompleted: {
        updateArtistModel()
    }

    Connections {
        target: mpdClient.albumModel
        function onModelReset() {
            updateArtistModel()
        }
    }

    function updateArtistModel() {
        artistModel.clear()
        const artists = []
        const seenArtists = new Set()
        for (let i = 0; i < mpdClient.albumModel.count; i++) {
            const album = mpdClient.albumModel.get(i)
            if (!album.artist) continue;
            if (!seenArtists.has(album.artist)) {
                seenArtists.add(album.artist)
                artists.push({
                    "name": album.artist
                })
            }
        }
        // Sort artists alphabetically
        artists.sort((a, b) => a.name.localeCompare(b.name))
        for (let i = 0; i < artists.length; i++) {
            artistModel.append(artists[i])
        }
    }

    // Filter albums by current artist
    function filterAlbumsByArtist(artistName) {
        filteredAlbumModel.clear()
        for (let i = 0; i < mpdClient.albumModel.count; i++) {
            let album = mpdClient.albumModel.get(i)
            if (album.artist === artistName) {
                filteredAlbumModel.append(album)
            }
        }
    }

    // Reset to all artists view
    function resetToArtistsView() {
        viewMode = "artists"
        currentArtist = ""
    }

    // Load albums for specific artist
    function loadArtistAlbums(artistName) {
        viewMode = "albums"
        currentArtist = artistName
        filterAlbumsByArtist(artistName)
    }

    // Header for artist-focused view
    Rectangle {
        id: artistHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 50
        color: palette.mid
        visible: viewMode === "albums"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Button {
                text: qsTr("← All Artists")
                onClicked: resetToArtistsView()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Return to artists view")
            }

            Label {
                text: currentArtist
                color: palette.windowText
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    // Artists Grid View
    GridView {
        id: artistGridView
        anchors.top: artistHeader.visible ? artistHeader.bottom : parent.top
        anchors.topMargin: artistHeader.visible ? 10 : 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        clip: true
        visible: viewMode === "artists"

        cellWidth: 200
        cellHeight: 100
        model: artistModel

        delegate: Item {
            width: artistGridView.cellWidth
            height: artistGridView.cellHeight

            property string artistName: model.name
            property string imageUrl: ""

            onArtistNameChanged: {
                imageUrl = "" // Reset image
                mpdClient.fetchArtistImage(artistName, function(artUrl) {
                    if (artUrl) {
                        imageUrl = artUrl;
                    }
                });
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 5
                color: palette.base
                radius: 5
                border.color: palette.accent
                clip: true

                Image {
                    id: artistImage
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    source: imageUrl
                    smooth: true
                    mipmap: true

                    // Placeholder if no image
                    Text {
                        anchors.centerIn: parent
                        text: model.name.substring(0, 1)
                        color: palette.text
                        font.pixelSize: 48
                        font.bold: true
                        visible: artistImage.status === Image.Null || artistImage.status === Image.Error
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 40
                    color: "#80000000"

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 10
                        text: model.name
                        color: "white"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 2
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: loadArtistAlbums(model.name)
                }
            }
        }

        ScrollBar.vertical: ScrollBar { }
    }

    // Albums Grid View
    GridView {
        id: albumGridView
        anchors.top: artistHeader.visible ? artistHeader.bottom : parent.top
        anchors.topMargin: artistHeader.visible ? 10 : 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        clip: true
        visible: viewMode === "albums"

        cellWidth: 180
        cellHeight: 220
        model: currentArtist ? filteredAlbumModel : mpdClient.albumModel

        delegate: Item {
            width: albumGridView.cellWidth
            height: albumGridView.cellHeight

            CheckBox {
                visible: selectionMode
                anchors.top: parent.top
                anchors.left: parent.left
                checked: selectedAlbums.indexOf(model.mbid) !== -1
                onCheckedChanged: function(checked) {
                    if (checked && selectedAlbums.indexOf(model.mbid) === -1) {
                        selectedAlbums.push(model.mbid);
                    } else if (!checked) {
                        selectedAlbums.splice(selectedAlbums.indexOf(model.mbid), 1);
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 10
                color: "transparent"

                Rectangle {
                    id: artContainer
                    height: width
                    width: parent.width
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: palette.base
                    radius: 5
                    border.width: model.art ? 2 : 0
                    border.color: palette.accent

                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: model.art
                        fillMode: Image.PreserveAspectCrop
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 10
                        text: model.name
                        color: palette.text
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        visible: !model.art
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                if (selectedAlbums.length > 0) {
                                    selectionMenu.popup()
                                } else {
                                    gridContextMenu.popup()
                                }
                            } else {
                                if (selectionMode) {
                                    var idx = selectedAlbums.indexOf(model.mbid);
                                    if (idx === -1) selectedAlbums.push(model.mbid);
                                    else selectedAlbums.splice(idx, 1);
                                } else {
                                    // Find the index in main album model to maintain compatibility
                                    let mainIndex = -1
                                    for (let i = 0; i < mpdClient.albumModel.count; i++) {
                                        if (mpdClient.albumModel.get(i).mbid === model.mbid) {
                                            mainIndex = i
                                            break
                                        }
                                    }
                                    if (mainIndex !== -1) {
                                        pathView.currentIndex = mainIndex
                                        coverFlow.viewMode = "flow"
                                    }
                                }
                            }
                        }

                        Menu {
                            id: gridContextMenu
                            MenuItem {
                                text: qsTr("Play Album")
                                onTriggered: mpdClient.playAlbum(model.artist, model.name, model.mbid)
                            }
                            MenuItem {
                                text: qsTr("Add to Queue")
                                onTriggered: mpdClient.addAlbum(model.artist, model.name, model.mbid)
                            }
                        }
                    }

                }

                Menu {
                    id: selectionMenu
                    MenuItem {
                        text: qsTr("Add selected to queue")
                        onTriggered: {
                            mpdClient.addAlbums(selectedAlbums);
                            selectedAlbums = [];
                            selectionMode = false;
                        }
                    }
                    MenuItem {
                        text: qsTr("Play selected")
                        onTriggered: {
                            mpdClient.playAlbums(selectedAlbums);
                            selectedAlbums = [];
                            selectionMode = false;
                        }
                    }
                    MenuItem {
                        text: qsTr("Clear selection")
                        onTriggered: {
                            selectedAlbums = [];
                        }
                    }
                }

                Text {
                    y: artContainer.y + artContainer.height + 5
                    width: parent.width
                    text: model.name
                    font.bold: true
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: palette.text
                }

                Text {
                    y: artContainer.y + artContainer.height + 22
                    width: parent.width
                    text: model.artist
                    font.pixelSize: 12 * window.fontScale
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    color: palette.windowText
                }
            }
        }

        ScrollBar.vertical: ScrollBar { }
    }
}
