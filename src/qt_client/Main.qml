import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import appExample

ApplicationWindow {
    id: root
    width: 800
    height: 700
    visible: true

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

            text: qsTr("ListFeatures")
        }

        Button {
            Layout.preferredHeight: 40
            Layout.preferredWidth: 200

            text: qsTr("RecordRoute")
        }

        Button {
            Layout.preferredHeight: 40
            Layout.preferredWidth: 200

            text: qsTr("RouteChat")
        }

        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            text: ServerComm.status
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
