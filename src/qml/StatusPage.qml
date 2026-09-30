import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import io.github.ramanenka.kamora

/**
 * Everything about one backup plan: what it is waiting for, which
 * drive it wants, and what is already in its repository.
 */
Kirigami.ScrollablePage {
    id: page

    required property BackupPlan plan

    title: plan.config.displayName

    // See SetupPage: a form is only as narrow as its widest child allows.
    readonly property int fieldWidth: Kirigami.Units.gridUnit * 24

    actions: [
        Kirigami.Action {
            text: page.plan.active ? "Cancel" : "Back up now"
            icon.name: page.plan.active ? "dialog-cancel" : "backup"
            enabled: page.plan.active || (!Kamora.anyRunning && page.plan.canBackupNow)
            onTriggered: page.plan.active
                ? page.plan.cancel()
                : page.plan.requestStart()
        },
        Kirigami.Action {
            text: "Configure…"
            icon.name: "configure"
            onTriggered: applicationWindow().openSetup(page.plan)
        },
        Kirigami.Action {
            text: "Show log"
            icon.name: "view-list-text"
            onTriggered: logSheet.open()
        }
    ]

    LogSheet {
        id: logSheet

        plan: page.plan
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !page.plan.borgAvailable
            type: Kirigami.MessageType.Error
            text: "borg is not installed, so no backup can run. Install the borgbackup package."
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.plan.drives.targetFilesystemChanged
            type: Kirigami.MessageType.Warning
            text: "This is the drive you configured, but it holds a different filesystem "
                + "than it did then - it has been reformatted or restored. If the "
                + "repository is not there any more, the next backup starts a new one "
                + "and the old archives are not part of it."
            actions: [
                Kirigami.Action {
                    text: "Reconfigure"
                    icon.name: "configure"
                    onTriggered: applicationWindow().openSetup(page.plan)
                }
            ]
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !page.plan.config.enabled && page.plan.config.configured
            type: Kirigami.MessageType.Information
            text: "This plan is switched off. It never runs on its own, "
                + "only when you start it from here."
        }

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Icon {
                        source: page.plan.statusIcon
                        implicitWidth: Kirigami.Units.iconSizes.huge
                        implicitHeight: Kirigami.Units.iconSizes.huge
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 2
                            text: page.plan.headline
                            wrapMode: Text.Wrap
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            text: page.plan.subtitle
                            wrapMode: Text.Wrap
                            elide: Text.ElideMiddle
                            maximumLineCount: 3
                            opacity: 0.8
                        }
                    }
                }

                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    visible: page.plan.runner.running
                    indeterminate: page.plan.runner.progress < 0
                    from: 0
                    to: 1
                    value: Math.max(0, page.plan.runner.progress)
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: page.plan.runner.running
                    text: page.plan.runner.stepLabel
                    opacity: 0.7
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !page.plan.active && page.plan.config.lastStatus === "failed"
            type: Kirigami.MessageType.Error
            text: page.plan.config.lastError.length > 0 ? page.plan.config.lastError
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
                text: page.plan.config.driveDisplay.length > 0 ? page.plan.config.driveDisplay
                                                              : page.plan.config.driveUuid
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            RowLayout {
                Kirigami.FormData.label: "State:"
                Layout.fillWidth: true
                Layout.maximumWidth: page.fieldWidth

                Kirigami.Icon {
                    source: !page.plan.drives.targetPresent ? "media-eject"
                        : (page.plan.drives.targetLocked ? "lock" : "media-mount")
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                    elide: Text.ElideMiddle
                    text: {
                        if (!page.plan.drives.targetPresent) {
                            return "not connected";
                        }
                        if (page.plan.drives.targetLocked) {
                            return "connected, locked";
                        }
                        return page.plan.drives.targetMounted
                            ? "connected, mounted at " + page.plan.drives.targetMountPoint
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
                        if (page.plan.repositoryPath.length > 0) {
                            return page.plan.repositoryPath;
                        }
                        return page.plan.config.repoPath.length > 0
                            ? page.plan.config.repoPath + " (on the drive)"
                            : "the top level of the drive";
                    }
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                }
            }

            RowLayout {
                QQC2.Button {
                    text: "Open repository"
                    icon.name: "folder-open"
                    visible: page.plan.repositoryPath.length > 0
                    onClicked: page.plan.openRepositoryFolder()
                }
            }

            Kirigami.Separator {
                Kirigami.FormData.label: "Schedule"
                Kirigami.FormData.isSection: true
            }

            QQC2.Label {
                Kirigami.FormData.label: "Last backup:"
                text: page.plan.lastBackupText
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Next backup:"
                text: page.plan.nextBackupText.length > 0 ? page.plan.nextBackupText : "—"
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Every:"
                text: page.plan.config.intervalHours
                    + (page.plan.config.intervalHours === 1 ? " hour" : " hours")
                textFormat: Text.PlainText
            }

            QQC2.Label {
                Kirigami.FormData.label: "Folders:"
                text: page.plan.config.includePaths.length + " included, "
                    + page.plan.config.excludePatterns.length + " exclusions"
                textFormat: Text.PlainText
            }
        }

        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            visible: page.plan.archives.length > 0
            text: "Archives in the repository"
        }

        Repeater {
            model: page.plan.archives

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
            visible: page.plan.archives.length === 0 && page.plan.drives.targetMounted
            text: page.plan.repositoryExists
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
