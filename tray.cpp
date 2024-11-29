/**
 * @file tray.cpp
 * @brief Main application file for the Yggdrasil Tray application.
 * 
 * Handles the GUI logic for managing the Yggdrasil service, including
 * displaying the system tray icon, managing menu interactions, and
 * updating the service status and IP address.
 */

#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QTimer>
#include <QClipboard>
#include <QIcon>
#include "ServiceManager.h"
#include "SocketManager.h"

/**
 * @brief Path to the Yggdrasil socket for communication.
 */
const QString YGG_SOCKET_PATH = "/var/run/yggdrasil.sock";

/**
 * @brief Icon to display when the Yggdrasil service is running.
 */
const QString ICON_RUNNING = "network-vpn";

/**
 * @brief Icon to display when the Yggdrasil service is not running.
 */
const QString ICON_NOT_RUNNING = "network-offline";

/**
 * @brief Tooltip for the tray icon.
 */
const QString TOOLTIP = "Yggdrasil Tray";

/**
 * @class YggdrasilTray
 * @brief Manages the system tray interface for the Yggdrasil service.
 * 
 * This class handles GUI interactions, service toggling, and updates
 * the tray icon based on the Yggdrasil service status.
 */
class YggdrasilTray : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the YggdrasilTray object.
     * @param parent Pointer to the parent QObject, if any.
     */
    explicit YggdrasilTray(QObject *parent = nullptr)
        : QObject(parent),
          serviceManager("yggdrasil"),
          socketManager(YGG_SOCKET_PATH) {
        trayIcon = new QSystemTrayIcon(this);
        trayIcon->setIcon(QIcon::fromTheme(ICON_NOT_RUNNING));
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
    /**
     * @brief Toggles the Yggdrasil service between running and stopped states.
     * 
     * This function checks the current status of the Yggdrasil service
     * and either starts or stops it. It updates the tray icon and displays
     * a message indicating the result of the operation.
     */
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

    /**
     * @brief Copies the Yggdrasil IP address to the clipboard.
     * 
     * Retrieves the Yggdrasil IP address from the socket manager and
     * copies it to the system clipboard. Displays a message indicating
     * the result of the operation.
     */
    void copyIP() {
        QString ip = socketManager.getYggdrasilIP();
        if (!ip.isEmpty()) {
            QApplication::clipboard()->setText(ip);
            QMessageBox::information(nullptr, "Copy IP", "IP copied to clipboard: " + ip);
        } else {
            QMessageBox::warning(nullptr, "Copy IP", "Failed to retrieve IP.");
        }
    }

    /**
     * @brief Updates the tray icon and menu items.
     * 
     * Retrieves the current Yggdrasil service status and IP address,
     * then updates the tray icon and menu items to reflect this information.
     */
    void updateTrayIcon() {
        QString status = serviceManager.isServiceRunning() ? "Running" : "Not Running";
        QString ip = socketManager.getYggdrasilIP();

        statusAction->setText("Status: " + status);
        ipAction->setText("IP: " + ip);

        trayIcon->setIcon(QIcon::fromTheme(status == "Running" ? ICON_RUNNING : ICON_NOT_RUNNING));
    }

    /**
     * @brief Handles tray icon activation events.
     * @param reason The reason for the activation event.
     * 
     * Displays the tray menu when the tray icon is activated
     * via a left or right click.
     */
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context) {
            trayMenu->popup(QCursor::pos());
        }
    }

private:
    QSystemTrayIcon *trayIcon;         ///< The system tray icon.
    QMenu *trayMenu;                   ///< The menu displayed in the system tray.
    QAction *statusAction;             ///< Action displaying the service status.
    QAction *ipAction;                 ///< Action displaying the Yggdrasil IP address.
    QAction *toggleAction;             ///< Action to toggle the Yggdrasil service.
    QAction *copyIPAction;             ///< Action to copy the Yggdrasil IP address.
    ServiceManager serviceManager;     ///< Manager for service-related tasks.
    SocketManager socketManager;       ///< Manager for socket-related tasks.
};

/**
 * @brief Main function for the Yggdrasil Tray application.
 * 
 * Initializes the QApplication and the YggdrasilTray instance,
 * then starts the event loop.
 * 
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code.
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Error", "System tray not available!");
        return 1;
    }

    QApplication::setQuitOnLastWindowClosed(false);

    YggdrasilTray tray;
    return app.exec();
}

#include "tray.moc"

