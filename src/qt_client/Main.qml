import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import appExample

ApplicationWindow {
    id: root
    width: 800
    height: 700
    visible: true

    Component.onCompleted: {
        ServerComm.readyChanged.connect(function() {
            console.log("ServerComm.readyChanged: ", ServerComm.ready)
            route.started = false;
            chat.started = false;
        })

        ServerComm.statusChanged.connect(function() {
            console.log("ServerComm.statusChanged: ", ServerComm.status)
        })

        ServerComm.errorChanged.connect(function() {
            console.log("ServerComm.errorChanged: ", ServerComm.error)
        })

        ServerComm.responseChanged.connect(function() {
            console.log("ServerComm.responseChanged: ", ServerComm.response)
        })

        ServerComm.onReceivedMessage.connect(function(message) {
            console.log("ServerComm.onReceivedMessage: ", message)
        })
    }

    ColumnLayout {
        x: 20
        y: 20
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Server")
            }
            Rectangle {
                Layout.preferredWidth: 300
                color: "lightblue"
                border.color: "grey"
                height: serverAddress.font.pixelSize + 10

                TextInput {
                    leftPadding: 10
                    anchors.fill: parent
                    id: serverAddress
                    width: 400
                    text: "http://localhost:10123"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Button {
                text: qsTr("Start Comm");

                onClicked: {
                    ServerComm.start(serverAddress.text)
                }
            }
        }

        Button {
            Layout.preferredHeight: 40
            Layout.preferredWidth: 200
            enabled: ServerComm.ready

            text: qsTr("GetFeature")

            onClicked: ServerComm.getFeature()
        }

        Button {
            Layout.preferredHeight: 40
            Layout.preferredWidth: 200
            enabled: ServerComm.ready

            text: qsTr("ListFeatures")
            onClicked: {
                ServerComm.listFeatures()
            }
        }

        RowLayout {
            id: route
            property bool started: false

            Button {
                Layout.preferredHeight: 40
                Layout.preferredWidth: 200
                enabled: ServerComm.ready && !route.started
                text: qsTr("RecordRoute")

                onClicked: {
                    route.started = true
                    ServerComm.recordRoute()
                }
            }

            Button {
                text: qsTr("Send message")
                enabled: route.started
                onClicked: {
                    ServerComm.sendRouteUpdate()
                }
            }

            Button {
                text: qsTr("Finish")
                enabled: route.started
                onClicked: {
                    ServerComm.finishRecordRoute()
                    route.started = false
                }
            }
        }

        RowLayout {
            id: chat
            property bool started: false
            Button {
                Layout.preferredHeight: 40
                Layout.preferredWidth: 200
                enabled: ServerComm.ready

                text: qsTr("RouteChat")

                onClicked: {
                    chat.started = true
                    ServerComm.routeChat()
                }
            }

            TextInput {
                id: chatMessage
                Layout.preferredWidth: 200
                enabled: chat.started
                text: "Hello from QML"
            }

            Button {
                text: qsTr("Send message")
                enabled: chat.started
                onClicked: {
                    ServerComm.sendChatMessage(chatMessage)
                }
            }

            Button {
                text: qsTr("Finish")
                enabled: chat.started
                onClicked: {
                    ServerComm.finishRouteChat()
                    chat.started = false
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            TextArea {
                id: messages
                text: ServerComm.status
                wrapMode: TextEdit.Wrap
                readOnly: true
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
