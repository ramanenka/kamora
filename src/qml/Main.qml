import QtQuick
import QtQuick.Controls as QQC2
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

    property bool showingStatus: false

    // The pages are dense enough that side-by-side columns only cramp them.
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    // The root page is the initial page, so that the common start-up path
    // never creates a page before the stack is there to hold it.
    pageStack.initialPage: Kamora.config.configured ? Qt.resolvedUrl("StatusPage.qml") : Qt.resolvedUrl("WelcomePage.qml")

    function showRoot(): void {
        showingStatus = Kamora.config.configured;
        pageStack.clear();
        pageStack.push(showingStatus ? Qt.resolvedUrl("StatusPage.qml") : Qt.resolvedUrl("WelcomePage.qml"));
    }

    function openSetup(): void {
        Kamora.config.beginEdit();
        Kamora.drives.refresh();
        pageStack.push(Qt.resolvedUrl("SetupPage.qml"));
    }

    Connections {
        target: Kamora.config

        function onChanged(): void {
            if (root.showingStatus !== Kamora.config.configured) {
                root.showRoot();
            }
        }
    }

    Connections {
        target: Kamora

        function onMessage(text: string, error: bool): void {
            root.showPassiveNotification(text, error ? "long" : "short");
        }

        function onConfigureRequested(): void {
            if (root.pageStack.depth < 2) {
                root.openSetup();
            }
        }
    }

    onClosing: close => {
        // Closing only puts Kamora back in the tray; it has to keep watching
        // for the drive. Without a configuration there is nothing to watch.
        if (Kamora.config.configured) {
            close.accepted = false;
            root.hide();
        }
    }

    Component.onCompleted: showingStatus = Kamora.config.configured
}
