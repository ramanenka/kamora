import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.ScrollablePage {
    id: page

    title: "Kamora Backup"

    // See SetupPage: a form is only as narrow as its widest child allows.
    readonly property int fieldWidth: Kirigami.Units.gridUnit * 24

    actions: [
        Kirigami.Action {
            text: Kamora.runner.running ? "Cancel" : "Back up now"
            icon.name: Kamora.runner.running ? "dialog-cancel" : "backup"
            enabled: Kamora.runner.running || Kamora.canBackupNow
            onTriggered: Kamora.runner.running ? Kamora.cancelBackup() : Kamora.startBackup()
        },
        Kirigami.Action {
            text: "Configure…"
            icon.name: "configure"
            onTriggered: applicationWindow().openSetup()
        },
        Kirigami.Action {
            text: "Show log"
            icon.name: "view-list-text"
            onTriggered: logSheet.open()
        }
    ]

    LogSheet {
        id: logSheet
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !Kamora.borgAvailable
            type: Kirigami.MessageType.Error
            text: "borg is not installed, so no backup can run. Install the borgbackup package."
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: Kamora.drives.targetFilesystemChanged
            type: Kirigami.MessageType.Warning
            text: "This is the drive you configured, but it holds a different filesystem "
                + "than it did then - it has been reformatted or restored. If the "
                + "repository is not there any more, the next backup starts a new one "
                + "and the old archives are not part of it."
            actions: [
                Kirigami.Action {
                    text: "Reconfigure"
                    icon.name: "configure"
                    onTriggered: Kamora.requestConfigure()
                }
            ]
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Icon {
                        source: Kamora.statusIcon
                        implicitWidth: Kirigami.Units.iconSizes.huge
                        implicitHeight: Kirigami.Units.iconSizes.huge
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 2
                            text: Kamora.headline
                            wrapMode: Text.Wrap
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            text: Kamora.subtitle
                            wrapMode: Text.Wrap
                            elide: Text.ElideMiddle
                            maximumLineCount: 3
                            opacity: 0.8
                        }
                    }
                }

                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    visible: Kamora.runner.running
                    indeterminate: Kamora.runner.progress < 0
                    from: 0
                    to: 1
                    value: Math.max(0, Kamora.runner.progress)
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: Kamora.runner.running
                    text: Kamora.runner.stepLabel
                    opacity: 0.7
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !Kamora.runner.running && Kamora.config.lastStatus === "failed"
            type: Kirigami.MessageType.Error
            text: Kamora.config.lastError.length > 0 ? Kamora.config.lastError
                                                     : "The last backup did not finish."
            actions: [
                Kirigami.Action {
                    text: "Show log"
                    icon.name: "view-list-text"
                    onTriggered: logSheet.open()
                }
            ]
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.label: "Backup drive"
                Kirigami.FormData.isSection: true
            }

            QQC2.Label {
                Kirigami.FormData.label: "Drive:"
                Layout.maximumWidth: page.fieldWidth
                text: Kamora.config.driveDisplay.length > 0 ? Kamora.config.driveDisplay
                                                            : Kamora.config.driveUuid
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            RowLayout {
                Kirigami.FormData.label: "State:"
                Layout.fillWidth: true
                Layout.maximumWidth: page.fieldWidth

                Kirigami.Icon {
                    source: !Kamora.drives.targetPresent ? "media-eject"
                        : (Kamora.drives.targetLocked ? "lock" : "media-mount")
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                    elide: Text.ElideMiddle
                    text: {
                        if (!Kamora.drives.targetPresent) {
                            return "not connected";
                        }
                        if (Kamora.drives.targetLocked) {
                            return "connected, locked";
                        }
                        return Kamora.drives.targetMounted
                            ? "connected, mounted at " + Kamora.drives.targetMountPoint
                            : "connected, not mounted";
                    }
                    textFormat: Text.PlainText
                }
            }

            RowLayout {
                Kirigami.FormData.label: "Repository:"
                Layout.fillWidth: true
                Layout.maximumWidth: page.fieldWidth

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                    text: {
                        if (Kamora.repositoryPath.length > 0) {
                            return Kamora.repositoryPath;
                        }
                        return Kamora.config.repoPath.length > 0
                            ? Kamora.config.repoPath + " (on the drive)"
                            : "the top level of the drive";
                    }
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                }
            }

            RowLayout {
                QQC2.Button {
                    text: Kamora.drives.targetLocked ? "Unlock and mount" : "Mount"
                    icon.name: Kamora.drives.targetLocked ? "unlock" : "media-mount"
                    visible: Kamora.drives.targetPresent && !Kamora.drives.targetMounted
                    enabled: !Kamora.drives.busy
                    onClicked: Kamora.mountDrive()
                }

                QQC2.Button {
                    text: Kamora.config.driveContainerUuid.length > 0 ? "Unmount and lock" : "Unmount"
                    icon.name: "media-eject"
                    visible: Kamora.drives.targetMounted
                    enabled: !Kamora.drives.busy && !Kamora.runner.running
                    onClicked: Kamora.unmountDrive()
                }

                QQC2.Button {
                    text: "Open repository"
                    icon.name: "folder-open"
                    visible: Kamora.repositoryPath.length > 0
                    onClicked: Kamora.openRepositoryFolder()
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.label: "Schedule"
                Kirigami.FormData.isSection: true
            }

            QQC2.Label {
                Kirigami.FormData.label: "Last backup:"
                text: Kamora.lastBackupText
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Next backup:"
                text: Kamora.nextBackupText
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Every:"
                text: Kamora.config.intervalHours + (Kamora.config.intervalHours === 1 ? " hour" : " hours")
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Folders:"
                text: Kamora.config.includePaths.length + " included, "
                    + Kamora.config.excludePatterns.length + " exclusions"
                textFormat: Text.PlainText
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            visible: Kamora.archives.length > 0
            text: "Archives in the repository"
        }

        Repeater {
            model: Kamora.archives

            delegate: RowLayout {
                id: archiveRow

                required property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    source: "package-x-generic"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                    text: archiveRow.modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                }

                QQC2.Label {
                    text: archiveRow.modelData.time.split(".")[0].replace("T", " ")
                    textFormat: Text.PlainText
                    opacity: 0.7
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: Kamora.archives.length === 0 && Kamora.drives.targetMounted
            text: Kamora.repositoryExists
                ? "The archive list could not be read from the repository."
                : "No repository on the drive yet — the first backup creates it."
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
