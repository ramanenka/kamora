import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.ScrollablePage {
    id: page

    title: Kamora.config.configured ? "Backup configuration" : "Add backup configuration"

    readonly property bool encrypted: Kamora.config.encryption !== "none"
    readonly property bool passphraseOk: !encrypted
        || (passphraseField.text.length === 0 && confirmField.text.length === 0)
        || passphraseField.text === confirmField.text
    readonly property bool driveKnown: driveCombo.currentIndex >= 0 || Kamora.config.driveUuid.length > 0
    readonly property bool canSave: driveKnown && Kamora.config.includePaths.length > 0 && passphraseOk

    function driveIndex(): int {
        const drives = Kamora.drives.availableDrives;
        for (let i = 0; i < drives.length; ++i) {
            if (drives[i].uuid === Kamora.config.driveUuid) {
                return i;
            }
        }
        return -1;
    }

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
            visible: !Kamora.borgAvailable
            type: Kirigami.MessageType.Warning
            text: "borg is not installed. Install the borgbackup package, otherwise "
                + "Kamora can store the configuration but not run a backup."
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Backup drive"
            Kirigami.FormData.isSection: true
        }

        RowLayout {
            Kirigami.FormData.label: "Drive:"
            Layout.fillWidth: true

            QQC2.ComboBox {
                id: driveCombo

                Layout.fillWidth: true
                model: Kamora.drives.availableDrives
                textRole: "display"
                currentIndex: page.driveIndex()
                displayText: {
                    if (currentIndex >= 0) {
                        return currentText;
                    }
                    if (Kamora.config.driveDisplay.length === 0) {
                        return "Select a drive…";
                    }
                    // Configured but missing from the list: either unplugged,
                    // or a fixed disk while only removable ones are listed.
                    return Kamora.config.driveDisplay
                        + (Kamora.drives.targetPresent ? " (connected)" : " (not connected)");
                }

                onActivated: index => Kamora.selectDrive(Kamora.drives.availableDrives[index])
            }

            QQC2.ToolButton {
                icon.name: "view-refresh"
                text: "Rescan"
                display: QQC2.AbstractButton.IconOnly
                onClicked: Kamora.drives.refresh()

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }

        QQC2.CheckBox {
            text: "Also list drives that are not removable"
            checked: Kamora.drives.showAllDrives
            onToggled: Kamora.drives.showAllDrives = checked
        }

        QQC2.Label {
            Kirigami.FormData.label: "Identified by:"
            text: Kamora.config.driveUuid.length > 0 ? "UUID " + Kamora.config.driveUuid
                                                     : "no drive selected yet"
            textFormat: Text.PlainText
            opacity: 0.8
        }

        Kirigami.InlineMessage {
            Kirigami.FormData.isSection: true
            Layout.fillWidth: true
            visible: Kamora.config.driveUuid.length > 0 && !Kamora.drives.targetPresent
            type: Kirigami.MessageType.Information
            text: "The configured drive is not connected right now. It stays selected "
                + "and Kamora will recognise it by its UUID when you plug it in."
        }

        Kirigami.Separator {
            Kirigami.FormData.label: "Repository"
            Kirigami.FormData.isSection: true
        }

        QQC2.TextField {
            Kirigami.FormData.label: "Path on the drive:"
            Layout.fillWidth: true
            text: Kamora.config.repoPath
            placeholderText: "kamora-borg-repo"
            onTextEdited: Kamora.config.repoPath = text
        }

        QQC2.Label {
            text: "The borg repository is created there on the first backup."
            opacity: 0.7
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        QQC2.ComboBox {
            id: encryptionCombo

            readonly property var keys: ["none", "repokey-blake2"]

            Kirigami.FormData.label: "Encryption:"
            Layout.fillWidth: true
            model: ["None — the drive is trusted", "Encrypted with a passphrase"]
            currentIndex: Math.max(0, keys.indexOf(Kamora.config.encryption))
            onActivated: index => Kamora.config.encryption = keys[index]
        }

        QQC2.TextField {
            id: passphraseField

            Kirigami.FormData.label: "Passphrase:"
            Layout.fillWidth: true
            visible: page.encrypted
            echoMode: TextInput.Password
            placeholderText: Kamora.config.configured ? "leave empty to keep the stored one" : ""
        }

        QQC2.TextField {
            id: confirmField

            Kirigami.FormData.label: "Repeat passphrase:"
            Layout.fillWidth: true
            visible: page.encrypted
            echoMode: TextInput.Password
        }

        QQC2.Label {
            visible: page.encrypted
            text: page.passphraseOk ? "Stored in KWallet."
                                    : "The two passphrases do not match."
            color: page.passphraseOk ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
            opacity: page.passphraseOk ? 0.7 : 1
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: "Compression:"
            Layout.fillWidth: true
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
            text: "Older archives are pruned after every backup."
            opacity: 0.7
            wrapMode: Text.Wrap
        }
    }
}
