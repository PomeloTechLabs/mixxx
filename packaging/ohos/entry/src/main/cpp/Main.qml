import QtQuick

Window {
    id: root
    width: 960
    height: 600
    visible: true
    color: "#101418"

    Rectangle {
        anchors.centerIn: parent
        width: 420
        height: 160
        radius: 12
        color: "#1d2733"
        border.color: "#c0392b"
        border.width: 2

        Column {
            anchors.centerIn: parent
            spacing: 12

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Mixxx"
                color: "#f0f4f8"
                font.pixelSize: 42
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Qt " + qtVersion + " on HarmonyOS - HAP shell OK"
                color: "#8fa3b8"
                font.pixelSize: 14
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "TASK-003 bring-up build"
                color: "#5d7285"
                font.pixelSize: 12
            }
        }
    }

    property string qtVersion: (typeof Qt !== "undefined") ? Qt.application.version : ""
}
