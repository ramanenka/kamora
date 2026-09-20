import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import io.github.ramanenka.kamora

/**
 * Settings that apply to Kamora itself rather than to one plan.
 */
Kirigami.ScrollablePage {
    id: page

    title: "Settings"

    readonly property int fieldWidth: Kirigami.Units.gridUnit * 24

    actions: [
        Kirigami.Action {
            text: "Done"
            icon.name: "dialog-ok"
            onTriggered: applicationWindow().pageStack.pop()
        }
    ]

    Kirigami.FormLayout {
        Kirigami.Separator {
            Kirigami.FormData.label: "Starting Kamora"
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            id: autostartBox

            text: "Start Kamora automatically at login"
            checked: Kamora.settings.autostart
            onToggled: Kamora.settings.autostart = checked
        }

        QQC2.CheckBox {
            text: "Start into the tray, without opening the window"
            enabled: autostartBox.checked
            checked: Kamora.settings.startInBackground
            onToggled: Kamora.settings.startInBackground = checked
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: "Kamora has to be running to notice that a backup drive was plugged "
                + "in. Autostart writes ~/.config/autostart/io.github.ramanenka.kamora.desktop."
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Backups"
            Kirigami.FormData.isSection: true
        }

        QQC2.Label {
            Kirigami.FormData.label: "Backup plans:"
            text: Kamora.planCount
            textFormat: Text.PlainText
        }

        QQC2.Label {
            Kirigami.FormData.label: "borg:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: Kamora.borgAvailable ? "installed" : "not installed — install the borgbackup package"
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: "Only one backup runs at a time. While one is going the others "
                + "cannot start, so they do not compete for the same disk."
            opacity: 0.7
            wrapMode: Text.Wrap
        }
    }
}
