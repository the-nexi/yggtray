/**
 * @file tray.cpp
 * @brief Main application file for the Yggdrasil Tray application.
 * 
 * Handles the GUI logic for managing the Yggdrasil service, including
 * displaying the system tray icon, managing menu interactions, and
 * updating the service status and IP address. Integrates a first-time
 * setup wizard to handle initial configuration tasks.
 */

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QTimer>
#include <QClipboard>
#include <QIcon>
#include <QStringList>
#include <QPixmap>
#include <cstdio>
#include "ServiceManager.h"
#include "SocketManager.h"
#include "SetupWizard.h"

/**
 * @brief Path candidates for the Yggdrasil socket.
 */
const QStringList POSSIBLE_YGG_SOCKET_PATHS = {
    "/var/run/yggdrasil.sock",    // Default path
    "/var/run/yggdrasil/yggdrasil.sock", // Ubuntu path
    "/run/yggdrasil.sock",        // Alternate path for some distributions
    "/tmp/yggdrasil.sock"         // Fallback path
};

/**
 * @brief Icon to display when the Yggdrasil service is running.
 */
const QString ICON_RUNNING = ":/icons/yggtray_running.png";

/**
 * @brief Icon to display when the Yggdrasil service is not running.
 */
const QString ICON_NOT_RUNNING = ":/icons/yggtray_not_running.png";

/**
 * @brief Tooltip for the tray icon.
 */
const QString TOOLTIP = "Yggdrasil Tray";

/**
 * @class YggdrasilTray
 * @brief Manages the system tray interface for the Yggdrasil service.
 */
class YggdrasilTray : public QObject {
    Q_OBJECT

public:
    explicit YggdrasilTray(QObject *parent = nullptr)
        : QObject(parent),
          serviceManager("yggdrasil"),
          socketManager(POSSIBLE_YGG_SOCKET_PATHS) {
        trayIcon = new QSystemTrayIcon(this);
        trayIcon->setIcon(QIcon(ICON_NOT_RUNNING));
        trayIcon->setToolTip(TOOLTIP);

        trayMenu = new QMenu();

        // Status menu item
        statusAction = new QAction("Status: Unknown", trayMenu);
        statusAction->setDisabled(true);
        trayMenu->addAction(statusAction);

        // IP address menu item
        ipAction = new QAction("IP: Unknown", trayMenu);
        ipAction->setDisabled(true);
        trayMenu->addAction(ipAction);

        trayMenu->addSeparator();

        // Toggle Yggdrasil service action
        toggleAction = new QAction("Start/Stop Yggdrasil", trayMenu);
        connect(toggleAction, &QAction::triggered, this, &YggdrasilTray::toggleYggdrasilService);
        trayMenu->addAction(toggleAction);

        // Copy IP action
        copyIPAction = new QAction("Copy IP", trayMenu);
        connect(copyIPAction, &QAction::triggered, this, &YggdrasilTray::copyIP);
        trayMenu->addAction(copyIPAction);

        trayMenu->addSeparator();

        // Quit action
        QAction *quitAction = new QAction("Quit", trayMenu);
        connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
        trayMenu->addAction(quitAction);

        trayIcon->setContextMenu(trayMenu);
        trayIcon->show();

        // Connect activated signal
        connect(trayIcon, &QSystemTrayIcon::activated, this, &YggdrasilTray::onTrayIconActivated);

        // Periodic update
        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &YggdrasilTray::updateTrayIcon);
        timer->start(5000);

        updateTrayIcon();
    }

private slots:
    void toggleYggdrasilService() {
        bool success;
        QString action;

        if (serviceManager.isServiceRunning()) {
            success = serviceManager.stopService();
            action = "stopped";
        } else {
            success = serviceManager.startService();
            action = "started";
        }

        if (success) {
            QMessageBox::information(nullptr, "Service Toggle", "Yggdrasil service successfully " + action + ".");
        } else {
            QMessageBox::critical(nullptr, "Service Toggle", "Failed to toggle Yggdrasil service.");
        }

        updateTrayIcon();
    }

    void copyIP() {
        QString ip = socketManager.getYggdrasilIP();
        if (!ip.isEmpty()) {
            QApplication::clipboard()->setText(ip);
            QMessageBox::information(nullptr, "Copy IP", "IP copied to clipboard: " + ip);
        } else {
            QMessageBox::warning(nullptr, "Copy IP", "Failed to retrieve IP.");
        }
    }

    void updateTrayIcon() {
        QString status = serviceManager.isServiceRunning() ? "Running" : "Not Running";
        QString ip = socketManager.getYggdrasilIP();

        statusAction->setText("Status: " + status);
        ipAction->setText("IP: " + ip);

        trayIcon->setIcon(QIcon(status == "Running" ? ICON_RUNNING : ICON_NOT_RUNNING));
    }

    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context) {
            trayMenu->popup(QCursor::pos());
        }
    }

private:
    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
    QAction *statusAction;
    QAction *ipAction;
    QAction *toggleAction;
    QAction *copyIPAction;
    ServiceManager serviceManager;
    SocketManager socketManager;
};

/**
 * @brief Main function for the Yggdrasil Tray application.
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Argument parsing
    bool forceSetup = false;
    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];

        if (arg == "--version") {
            printf("yggtray version %s\n", YGGTRAY_VERSION);
            return 0;
        }

        if (arg == "--setup") {
            forceSetup = true;
        }
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Error", "System tray is not available on this system.");
        return 1;
    }

    QApplication::setQuitOnLastWindowClosed(false);

    SetupWizard wizard;
    wizard.run(forceSetup);

    YggdrasilTray tray;

    return app.exec();
}

#include "tray.moc"

