import QtQuick
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.Page {
    id: page

    title: "Kamora Backup"

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4

        icon.name: "backup"
        text: "No backup configuration yet"
        explanation: "Kamora keeps a borg repository on a USB drive up to date. "
            + "Tell it which drive to use and which folders to keep, and it will "
            + "run the backup whenever the drive is plugged in and a backup is due."

        helpfulAction: Kirigami.Action {
            icon.name: "list-add"
            text: "Add backup configuration"
            onTriggered: applicationWindow().openSetup()
        }
    }
}
