import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import io.github.ramanenka.kamora

/**
 * The list of backup plans, and the way to every other page.
 */
Kirigami.ScrollablePage {
    id: page

    title: "Kamora Backup"

    actions: [
        Kirigami.Action {
            text: "Add plan"
            icon.name: "list-add"
            onTriggered: applicationWindow().addPlan()
        },
        Kirigami.Action {
            text: "Settings"
            icon.name: "configure"
            onTriggered: applicationWindow().openSettings()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !Kamora.borgAvailable && Kamora.planCount > 0
            type: Kirigami.MessageType.Error
            text: "borg is not installed, so no backup can run. Install the borgbackup package."
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 4
            visible: Kamora.planCount === 0

            icon.name: "backup"
            text: "No backup plans yet"
            explanation: "Kamora keeps borg repositories on USB drives up to date. Tell it "
                + "which drive to use and which folders to keep, and it runs the backup "
                + "whenever the drive is plugged in and a backup is due. Add as many "
                + "plans as you have drives."

            helpfulAction: Kirigami.Action {
                icon.name: "list-add"
                text: "Add backup plan"
                onTriggered: applicationWindow().addPlan()
            }
        }

        Repeater {
            model: Kamora.plans

            delegate: Kirigami.AbstractCard {
                id: card

                required property BackupPlan modelData
                readonly property BackupPlan plan: modelData

                Layout.fillWidth: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            source: card.plan.statusIcon
                            implicitWidth: Kirigami.Units.iconSizes.large
                            implicitHeight: Kirigami.Units.iconSizes.large
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Kirigami.Heading {
                                Layout.fillWidth: true
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                level: 3
                                text: card.plan.config.displayName
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                text: card.plan.headline
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                text: card.plan.subtitle
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                opacity: 0.7
                            }
                        }
                    }

                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        visible: card.plan.runner.running
                        indeterminate: card.plan.runner.progress < 0
                        from: 0
                        to: 1
                        value: Math.max(0, card.plan.runner.progress)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                            text: "Last backup: " + card.plan.lastBackupText
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            opacity: 0.7
                        }

                        QQC2.Button {
                            text: card.plan.active ? "Cancel" : "Back up now"
                            icon.name: card.plan.active ? "dialog-cancel" : "backup"
                            // Only one backup runs at a time, so every other
                            // card's button rests while one is going.
                            enabled: card.plan.active
                                || (!Kamora.anyRunning && card.plan.canBackupNow)
                            onClicked: card.plan.active
                                ? card.plan.cancel()
                                : card.plan.requestStart()
                        }

                        QQC2.Button {
                            text: "Details"
                            icon.name: "documentinfo"
                            onClicked: applicationWindow().openPlan(card.plan)
                        }

                        QQC2.Button {
                            text: "Configure…"
                            icon.name: "configure"
                            onClicked: applicationWindow().openSetup(card.plan)
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
