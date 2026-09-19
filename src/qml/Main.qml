import QtQuick
import org.kde.kirigami as Kirigami
import org.kamora.backup

Kirigami.ApplicationWindow {
    id: root

    title: "Kamora Backup"

    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 26
    width: Kirigami.Units.gridUnit * 44
    height: Kirigami.Units.gridUnit * 36

    // The window is opened explicitly by the controller, so that starting
    // with --background only puts the app in the tray.
    visible: false

    // The pages are dense enough that side-by-side columns only cramp them.
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    // The list of plans is always the root: it is the one page that
    // makes sense whether there are none, one or several.
    pageStack.initialPage: Qt.resolvedUrl("OverviewPage.qml")

    function showRoot(): void {
        pageStack.clear();
        pageStack.push(Qt.resolvedUrl("OverviewPage.qml"));
    }

    function openPlan(plan): void {
        pageStack.push(Qt.resolvedUrl("StatusPage.qml"), { plan: plan });
    }

    function openSetup(plan): void {
        plan.config.beginEdit();
        plan.drives.refresh();
        pageStack.push(Qt.resolvedUrl("SetupPage.qml"), { plan: plan });
    }

    function addPlan(): void {
        openSetup(Kamora.addPlan());
    }

    /// Takes the pages showing this plan down before it is deleted.
    function removePlan(plan): void {
        showRoot();
        Kamora.removePlan(plan);
    }

    function openSettings(): void {
        pageStack.push(Qt.resolvedUrl("SettingsPage.qml"));
    }

    Connections {
        target: Kamora

        function onMessage(text: string, error: bool): void {
            root.showPassiveNotification(text, error ? "long" : "short");
        }
    }

    onClosing: close => {
        // Closing only puts Kamora back in the tray; it has to keep watching
        // for the drives. Without a plan there is nothing to watch.
        if (Kamora.configured) {
            close.accepted = false;
            root.hide();
        }
    }
}
