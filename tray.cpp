#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <QClipboard>
#include <QIcon>
#include <QProcess>
#include <QHostAddress>

const QString YGG_SOCKET_PATH = "/var/run/yggdrasil.sock";

class YggdrasilTray : public QObject {
    Q_OBJECT

public:
    explicit YggdrasilTray(QObject *parent = nullptr) : QObject(parent) {
        trayIcon = new QSystemTrayIcon(this);
        trayIcon->setIcon(QIcon::fromTheme("network-transmit-receive"));
        trayIcon->setToolTip("Yggdrasil Tray");

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
        connect(toggleAction, &QAction::triggered, this, 
				&YggdrasilTray::toggleYggdrasilService);
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
        // Check the actual status of the Yggdrasil service using systemctl
        QProcess statusProcess;
        QStringList statusArguments = {"is-active", "yggdrasil"};
        statusProcess.start("systemctl", statusArguments);
        statusProcess.waitForFinished();

        QString serviceStatus = statusProcess.readAllStandardOutput().trimmed();

        QString action;
        QStringList arguments;
        if (serviceStatus == "active") {
            action = "stop";
            arguments = QStringList() << "systemctl" << "stop" << "yggdrasil";
        } else {
            action = "start";
            arguments = QStringList() << "systemctl" << "start" << "yggdrasil";
        }

        QProcess process;
        process.start("pkexec", arguments);
        process.waitForFinished();

        if (process.exitCode() == 0) {
            QMessageBox::information(nullptr, "Service Toggle", "Successfully " + action + "ed Yggdrasil service.");
        } else {
            QMessageBox::critical(nullptr, "Service Toggle", "Failed to " + action + " Yggdrasil service.");
        }

        updateTrayIcon();
    }

    void copyIP() {
        QString ip = getYggdrasilIP();
        if (!ip.isEmpty()) {
            QApplication::clipboard()->setText(ip);
            QMessageBox::information(nullptr, "Copy IP", "IP copied to clipboard: " + ip);
        } else {
            QMessageBox::warning(nullptr, "Copy IP", "Failed to retrieve IP.");
        }
    }

    void updateTrayIcon() {
        QString status = getYggdrasilStatus();
        QString ip = getYggdrasilIP();

        statusAction->setText("Status: " + status);
        ipAction->setText("IP: " + ip);

        if (status.contains("Running")) {
            trayIcon->setIcon(QIcon::fromTheme("network-transmit-receive"));
        } else {
            trayIcon->setIcon(QIcon::fromTheme("network-offline"));
        }
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
    QAction *ipAction;  // Action to display IP address
    QAction *toggleAction;
    QAction *copyIPAction;

    QString getYggdrasilStatus() {
        QJsonObject response = sendRequest({{"request", "getself"}});
        return response.contains("response") ? "Running" : "Not Running";
    }

    QString getYggdrasilIP() {
        QJsonObject response = sendRequest({{"request", "getself"}});
        if (response.contains("response")) {
            return response["response"].toObject()["address"].toString();
        }
        return "Unknown";
    }

    QJsonObject sendRequest(const QJsonObject &request) {
        QLocalSocket socket;
        socket.connectToServer(YGG_SOCKET_PATH);

        if (!socket.waitForConnected(3000)) {
            return {};
        }

        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
        if (!socket.waitForBytesWritten(3000)) {
            return {};
        }

        if (!socket.waitForReadyRead(3000)) {
            return {};
        }

        QByteArray responseData = socket.readAll();
        return QJsonDocument::fromJson(responseData).object();
    }
};

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

