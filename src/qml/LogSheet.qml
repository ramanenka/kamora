import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.Dialog {
    id: dialog

    title: "Backup log"
    preferredWidth: Kirigami.Units.gridUnit * 44
    preferredHeight: Kirigami.Units.gridUnit * 26

    standardButtons: Kirigami.Dialog.Close
    customFooterActions: [
        Kirigami.Action {
            text: "Clear"
            icon.name: "edit-clear-history"
            onTriggered: Kamora.clearLog()
        }
    ]

    QQC2.ScrollView {
        contentWidth: availableWidth

        QQC2.TextArea {
            readOnly: true
            wrapMode: TextEdit.Wrap
            font.family: "monospace"
            text: Kamora.logText.length > 0 ? Kamora.logText : "Nothing logged yet."

            // Keep the newest lines in view while a backup is running.
            onTextChanged: cursorPosition = length
        }
    }
}
