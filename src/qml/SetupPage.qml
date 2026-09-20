import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import io.github.ramanenka.kamora

Kirigami.ScrollablePage {
    id: page

    required property BackupPlan plan

    title: plan.config.configured ? "Backup plan" : "Add backup plan"

    readonly property bool canSave: plan.config.driveUuid.length > 0
        && plan.config.includePaths.length > 0

    /// Result of the last folder pick, used for the notes below the picker.
    property var lastPick: null

    // Kirigami.FormLayout takes its width from the widest child's implicit
    // width, so anything holding long text has to be capped or it pushes the
    // whole form past the edge of the window.
    readonly property int fieldWidth: Kirigami.Units.gridUnit * 24

    actions: [
        Kirigami.Action {
            text: "Save"
            icon.name: "document-save"
            enabled: page.canSave
            onTriggered: {
                page.plan.saveConfiguration();
                applicationWindow().pageStack.pop();
            }
        },
        Kirigami.Action {
            text: "Cancel"
            icon.name: "dialog-cancel"
            onTriggered: {
                const plan = page.plan;
                plan.config.rollback();
                applicationWindow().pageStack.pop();
                // A plan that was added and then abandoned never
                // existed as far as the user is concerned.
                Kamora.discardIfUnconfigured(plan);
            }
        },
        Kirigami.Action {
            text: "Remove plan"
            icon.name: "edit-delete"
            visible: page.plan.config.configured
            onTriggered: forgetDialog.open()
        }
    ]

    FolderDialog {
        id: repositoryDialog

        title: "Choose the borg repository folder on the drive"
        currentFolder: page.plan.browseStartFolder

        onAccepted: page.lastPick = page.plan.selectRepositoryFolder(selectedFolder)
    }

    Kirigami.PromptDialog {
        id: forgetDialog

        // A dialog declared inside a ScrollablePage would otherwise have its
        // footer laid out in the page content.
        parent: applicationWindow().overlay

        title: "Remove backup plan?"
        subtitle: "Kamora will stop watching for this drive. The archives already "
            + "in the repository are left untouched."
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: "Remove"
                icon.name: "edit-delete"
                onTriggered: {
                    forgetDialog.close();
                    applicationWindow().removePlan(page.plan);
                }
            }
        ]
    }

    Kirigami.FormLayout {
        id: form

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: !page.plan.borgAvailable
            type: Kirigami.MessageType.Warning
            text: "borg is not installed. Install the borgbackup package, otherwise "
                + "Kamora can store the plan but not run a backup."
        }

        QQC2.TextField {
            Kirigami.FormData.label: "Name:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: page.plan.config.name
            placeholderText: page.plan.config.displayName
            onTextEdited: page.plan.config.name = text
        }

        QQC2.CheckBox {
            text: "Run this plan automatically"
            checked: page.plan.config.enabled
            onToggled: page.plan.config.enabled = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Repository"
            Kirigami.FormData.isSection: true
        }

        QQC2.Label {
            Kirigami.FormData.label: "Folder:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: {
                if (page.plan.config.driveUuid.length === 0) {
                    return "Not chosen yet";
                }
                return page.plan.config.repoPath.length > 0
                    ? page.plan.config.repoPath
                    : "the top level of the drive";
            }
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
        }

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: page.plan.config.driveUuid.length > 0 ? "Change…" : "Choose folder…"
                icon.name: "folder-open"
                onClicked: repositoryDialog.open()
            }

            QQC2.Button {
                text: "Mount drive"
                icon.name: "media-mount"
                visible: page.plan.drives.targetPresent && !page.plan.drives.targetMounted
                enabled: !page.plan.drives.busy
                onClicked: page.plan.mountDrive()
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: "On drive:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: {
                if (page.plan.config.driveDisplay.length === 0) {
                    return "—";
                }
                return page.plan.config.driveDisplay
                    + (page.plan.drives.targetPresent ? " (connected)" : " (not connected)");
            }
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
        }

        QQC2.Label {
            Kirigami.FormData.label: "Identified by:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            elide: Text.ElideRight
            text: {
                if (page.plan.config.driveUuid.length === 0) {
                    return "no drive chosen yet";
                }
                if (page.plan.config.driveContainerUuid.length > 0) {
                    return "UUID " + page.plan.config.driveUuid
                        + ", in LUKS " + page.plan.config.driveContainerUuid;
                }
                return "UUID " + page.plan.config.driveUuid;
            }
            textFormat: Text.PlainText
            opacity: 0.8
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: "Pick the folder on the drive itself. Kamora stores the drive's UUID "
                + "and the path within it, so the repository is found again whatever "
                + "device node or mount point the drive gets next time."
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.lastPick !== null && !page.lastPick.found
            type: Kirigami.MessageType.Error
            text: "That folder is not on a mounted drive, so there is no drive to "
                + "remember it by."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.lastPick !== null && page.lastPick.found && !page.lastPick.removable
            type: Kirigami.MessageType.Warning
            text: "That folder is on a fixed disk rather than a removable drive. It "
                + "works, but the drive will then always count as connected."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.lastPick !== null && page.lastPick.found && page.lastPick.encrypted
            type: Kirigami.MessageType.Information
            text: "That drive is encrypted. Kamora remembers it by its LUKS header as "
                + "well, so it is recognised while still locked, and the system asks "
                + "to unlock it when a backup needs the drive."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.plan.config.driveUuid.length > 0 && page.plan.config.repoPath.length === 0
            type: Kirigami.MessageType.Warning
            text: "That is the top level of the drive. borg needs a directory of its "
                + "own, so pick or create a subfolder unless the drive is empty."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.plan.config.driveUuid.length > 0 && !page.plan.drives.targetPresent
            type: Kirigami.MessageType.Information
            text: "The drive is not connected right now. The choice stays as it is and "
                + "Kamora will recognise the drive by its UUID when you plug it in."
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: "Compression:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            model: ["zstd", "lz4", "zlib", "none"]
            currentIndex: Math.max(0, model.indexOf(page.plan.config.compression))
            onActivated: index => page.plan.config.compression = model[index]
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Folders to back up"
            Kirigami.FormData.isSection: true
        }

        PathListEditor {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth

            entries: page.plan.config.includePaths
            emptyText: "Add at least one folder to back up."
            addFolderText: "Add folder…"

            onRemoveRequested: index => page.plan.config.removeIncludePath(index)
            onFolderAdded: folder => page.plan.config.addIncludePath(folder)
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Excluded from the backup"
            Kirigami.FormData.isSection: true
        }

        PathListEditor {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth

            entries: page.plan.config.excludePatterns
            allowPatterns: true
            emptyText: "Nothing is excluded."
            addFolderText: "Exclude folder…"
            patternPlaceholder: "borg pattern, for example sh:**/build"

            onRemoveRequested: index => page.plan.config.removeExcludePattern(index)
            onFolderAdded: folder => page.plan.config.addExcludeFolder(folder)
            onPatternAdded: pattern => page.plan.config.addExcludePattern(pattern)
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Schedule"
            Kirigami.FormData.isSection: true
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Back up every:"
            from: 1
            to: 24 * 60
            stepSize: 1
            value: page.plan.config.intervalHours
            textFromValue: (value, locale) => value + (value === 1 ? " hour" : " hours")
            valueFromText: text => parseInt(text, 10)
            onValueModified: page.plan.config.intervalHours = value
        }

        QQC2.CheckBox {
            text: "Start the backup on its own when the drive is connected"
            checked: page.plan.config.backupOnConnect
            onToggled: page.plan.config.backupOnConnect = checked
        }

        QQC2.CheckBox {
            text: "Unmount the drive when the backup is done"
            checked: page.plan.config.unmountAfter
            onToggled: page.plan.config.unmountAfter = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Archives to keep"
            Kirigami.FormData.isSection: true
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Daily:"
            from: 0
            to: 365
            value: page.plan.config.keepDaily
            onValueModified: page.plan.config.keepDaily = value
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Weekly:"
            from: 0
            to: 520
            value: page.plan.config.keepWeekly
            onValueModified: page.plan.config.keepWeekly = value
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Monthly:"
            from: 0
            to: 240
            value: page.plan.config.keepMonthly
            onValueModified: page.plan.config.keepMonthly = value
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: "Older archives are pruned after every backup."
            opacity: 0.7
            wrapMode: Text.Wrap
        }
    }
}
