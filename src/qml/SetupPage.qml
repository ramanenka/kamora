import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.ScrollablePage {
    id: page

    title: Kamora.config.configured ? "Backup configuration" : "Add backup configuration"

    readonly property bool encrypted: Kamora.config.encryption !== "none"
    readonly property bool passphraseOk: !encrypted
        || (passphraseField.text.length === 0 && confirmField.text.length === 0)
        || passphraseField.text === confirmField.text
    readonly property bool canSave: Kamora.config.driveUuid.length > 0
        && Kamora.config.includePaths.length > 0 && passphraseOk

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
                const wasConfigured = Kamora.config.configured;
                Kamora.saveConfiguration(passphraseField.text);
                passphraseField.clear();
                confirmField.clear();
                if (wasConfigured) {
                    applicationWindow().pageStack.pop();
                }
            }
        },
        Kirigami.Action {
            text: "Cancel"
            icon.name: "dialog-cancel"
            onTriggered: {
                Kamora.config.rollback();
                applicationWindow().pageStack.pop();
            }
        },
        Kirigami.Action {
            text: "Remove configuration"
            icon.name: "edit-delete"
            visible: Kamora.config.configured
            onTriggered: forgetDialog.open()
        }
    ]

    FolderDialog {
        id: repositoryDialog

        title: "Choose the borg repository folder on the drive"
        currentFolder: Kamora.browseStartFolder

        onAccepted: page.lastPick = Kamora.selectRepositoryFolder(selectedFolder)
    }

    Kirigami.PromptDialog {
        id: forgetDialog

        // A dialog declared inside a ScrollablePage would otherwise have its
        // footer laid out in the page content.
        parent: applicationWindow().overlay

        title: "Remove backup configuration?"
        subtitle: "Kamora will stop watching for the drive. The archives already "
            + "in the repository are left untouched."
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: "Remove"
                icon.name: "edit-delete"
                onTriggered: {
                    forgetDialog.close();
                    Kamora.forgetConfiguration();
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
            visible: !Kamora.borgAvailable
            type: Kirigami.MessageType.Warning
            text: "borg is not installed. Install the borgbackup package, otherwise "
                + "Kamora can store the configuration but not run a backup."
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
                if (Kamora.config.driveUuid.length === 0) {
                    return "Not chosen yet";
                }
                return Kamora.config.repoPath.length > 0
                    ? Kamora.config.repoPath
                    : "the top level of the drive";
            }
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
        }

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: Kamora.config.driveUuid.length > 0 ? "Change…" : "Choose folder…"
                icon.name: "folder-open"
                onClicked: repositoryDialog.open()
            }

            QQC2.Button {
                text: "Mount drive"
                icon.name: "media-mount"
                visible: Kamora.drives.targetPresent && !Kamora.drives.targetMounted
                enabled: !Kamora.drives.busy
                onClicked: Kamora.mountDrive()
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: "On drive:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            text: {
                if (Kamora.config.driveDisplay.length === 0) {
                    return "—";
                }
                return Kamora.config.driveDisplay
                    + (Kamora.drives.targetPresent ? " (connected)" : " (not connected)");
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
                if (Kamora.config.driveUuid.length === 0) {
                    return "no drive chosen yet";
                }
                if (Kamora.config.driveContainerUuid.length > 0) {
                    return "UUID " + Kamora.config.driveUuid
                        + ", in LUKS " + Kamora.config.driveContainerUuid;
                }
                return "UUID " + Kamora.config.driveUuid;
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
                + "well, so it is recognised while still locked, and asks for the "
                + "passphrase when a backup needs it."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: Kamora.config.driveUuid.length > 0 && Kamora.config.repoPath.length === 0
            type: Kirigami.MessageType.Warning
            text: "That is the top level of the drive. borg needs a directory of its "
                + "own, so pick or create a subfolder unless the drive is empty."
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: Kamora.config.driveUuid.length > 0 && !Kamora.drives.targetPresent
            type: Kirigami.MessageType.Information
            text: "The drive is not connected right now. The choice stays as it is and "
                + "Kamora will recognise the drive by its UUID when you plug it in."
        }

        QQC2.ComboBox {
            id: encryptionCombo

            readonly property var keys: ["none", "repokey-blake2"]

            Kirigami.FormData.label: "Encryption:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            model: ["None — the drive is trusted", "Encrypted with a passphrase"]
            currentIndex: Math.max(0, keys.indexOf(Kamora.config.encryption))
            onActivated: index => Kamora.config.encryption = keys[index]
        }

        QQC2.TextField {
            id: passphraseField

            Kirigami.FormData.label: "Passphrase:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.encrypted
            echoMode: TextInput.Password
            placeholderText: Kamora.config.configured ? "leave empty to keep the stored one" : ""
        }

        QQC2.TextField {
            id: confirmField

            Kirigami.FormData.label: "Repeat passphrase:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            visible: page.encrypted
            echoMode: TextInput.Password
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            wrapMode: Text.Wrap
            visible: page.encrypted
            text: page.passphraseOk ? "Stored in KWallet."
                                    : "The two passphrases do not match."
            color: page.passphraseOk ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
            opacity: page.passphraseOk ? 0.7 : 1
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: "Compression:"
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth
            model: ["zstd", "lz4", "zlib", "none"]
            currentIndex: Math.max(0, model.indexOf(Kamora.config.compression))
            onActivated: index => Kamora.config.compression = model[index]
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Folders to back up"
            Kirigami.FormData.isSection: true
        }

        PathListEditor {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth

            entries: Kamora.config.includePaths
            emptyText: "Add at least one folder to back up."
            addFolderText: "Add folder…"

            onRemoveRequested: index => Kamora.config.removeIncludePath(index)
            onFolderAdded: folder => Kamora.config.addIncludePath(folder)
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Excluded from the backup"
            Kirigami.FormData.isSection: true
        }

        PathListEditor {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            Layout.maximumWidth: page.fieldWidth

            entries: Kamora.config.excludePatterns
            allowPatterns: true
            emptyText: "Nothing is excluded."
            addFolderText: "Exclude folder…"
            patternPlaceholder: "borg pattern, for example sh:**/build"

            onRemoveRequested: index => Kamora.config.removeExcludePattern(index)
            onFolderAdded: folder => Kamora.config.addExcludeFolder(folder)
            onPatternAdded: pattern => Kamora.config.addExcludePattern(pattern)
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
            value: Kamora.config.intervalHours
            textFromValue: (value, locale) => value + (value === 1 ? " hour" : " hours")
            valueFromText: text => parseInt(text, 10)
            onValueModified: Kamora.config.intervalHours = value
        }

        QQC2.CheckBox {
            text: "Start the backup on its own when the drive is connected"
            checked: Kamora.config.backupOnConnect
            onToggled: Kamora.config.backupOnConnect = checked
        }

        QQC2.CheckBox {
            text: "Unmount the drive when the backup is done"
            checked: Kamora.config.unmountAfter
            onToggled: Kamora.config.unmountAfter = checked
        }

        QQC2.CheckBox {
            text: "Start Kamora automatically at login"
            checked: Kamora.config.autostart
            onToggled: Kamora.config.autostart = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Archives to keep"
            Kirigami.FormData.isSection: true
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Daily:"
            from: 0
            to: 365
            value: Kamora.config.keepDaily
            onValueModified: Kamora.config.keepDaily = value
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Weekly:"
            from: 0
            to: 520
            value: Kamora.config.keepWeekly
            onValueModified: Kamora.config.keepWeekly = value
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: "Monthly:"
            from: 0
            to: 240
            value: Kamora.config.keepMonthly
            onValueModified: Kamora.config.keepMonthly = value
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
