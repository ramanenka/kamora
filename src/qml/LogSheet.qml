import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.Dialog {
    id: dialog

    required property BackupPlan plan

    title: "Backup log — " + plan.config.displayName
    preferredWidth: Kirigami.Units.gridUnit * 44
    preferredHeight: Kirigami.Units.gridUnit * 26

    standardButtons: Kirigami.Dialog.Close
    customFooterActions: [
        Kirigami.Action {
            text: "Clear"
            icon.name: "edit-clear-history"
            onTriggered: dialog.plan.clearLog()
        }
    ]

    QQC2.ScrollView {
        contentWidth: availableWidth

        QQC2.TextArea {
            readOnly: true
            wrapMode: TextEdit.Wrap
            font.family: "monospace"
            text: dialog.plan.logText.length > 0 ? dialog.plan.logText : "Nothing logged yet."

            // Keep the newest lines in view while a backup is running.
            onTextChanged: cursorPosition = length
        }
    }
}
