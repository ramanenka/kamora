import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

/**
 * Edits a list of strings that are either folders or borg exclude patterns.
 */
ColumnLayout {
    id: editor

    property var entries: []
    /// Show a text field for typing patterns by hand.
    property bool allowPatterns: false
    property string addFolderText: "Add folder…"
    property string emptyText: "Nothing here yet"
    property string patternPlaceholder: "Pattern, for example sh:**/build"

    signal removeRequested(int index)
    signal folderAdded(url folder)
    signal patternAdded(string pattern)

    spacing: Kirigami.Units.smallSpacing

    QQC2.Label {
        Layout.fillWidth: true
        visible: editor.entries.length === 0
        text: editor.emptyText
        opacity: 0.7
        wrapMode: Text.Wrap
    }

    Repeater {
        model: editor.entries

        delegate: RowLayout {
            id: row

            required property string modelData
            required property int index

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
                source: editor.allowPatterns ? "edit-find" : "folder"
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: row.modelData
                elide: Text.ElideMiddle
                textFormat: Text.PlainText

                QQC2.ToolTip.text: row.modelData
                QQC2.ToolTip.visible: hovered.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: hovered
                }
            }

            QQC2.ToolButton {
                icon.name: "list-remove"
                display: QQC2.AbstractButton.IconOnly
                text: "Remove"
                onClicked: editor.removeRequested(row.index)

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: patternField

            Layout.fillWidth: true
            visible: editor.allowPatterns
            placeholderText: editor.patternPlaceholder

            onAccepted: {
                if (text.length > 0) {
                    editor.patternAdded(text);
                    clear();
                }
            }
        }

        QQC2.Button {
            visible: editor.allowPatterns
            text: "Add"
            icon.name: "list-add"
            enabled: patternField.text.length > 0
            onClicked: {
                editor.patternAdded(patternField.text);
                patternField.clear();
            }
        }

        Item {
            Layout.fillWidth: !editor.allowPatterns
        }

        QQC2.Button {
            text: editor.addFolderText
            icon.name: "folder-add"
            onClicked: folderDialog.open()
        }
    }

    FolderDialog {
        id: folderDialog

        title: editor.addFolderText
        onAccepted: editor.folderAdded(selectedFolder)
    }
}
