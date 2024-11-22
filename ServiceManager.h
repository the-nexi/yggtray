#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include <QString>
#include <QProcess>
#include <QDebug>

class ServiceManager {
public:
    ServiceManager(const QString &serviceName) : serviceName(serviceName) {}

    // Check if the service is running
    bool isServiceRunning() const {
        QProcess process;
        QStringList arguments = {"is-active", serviceName};
        process.start("systemctl", arguments);
        process.waitForFinished();

        QString output = process.readAllStandardOutput().trimmed();
        return (output == "active");
    }

    // Start the service
    bool startService() const {
        return executeCommand("start");
    }

    // Stop the service
    bool stopService() const {
        return executeCommand("stop");
    }

private:
    QString serviceName;

    bool executeCommand(const QString &action) const {
        QProcess process;
        QStringList arguments = {"systemctl", action, serviceName}; // Properly pass all arguments
        process.start("pkexec", arguments);
        process.waitForFinished();

        if (process.exitCode() == 0) {
            qDebug() << action << "command executed successfully for" << serviceName;
            return true;
        } else {
            qDebug() << action << "command failed for" << serviceName << ":" << process.readAllStandardError();
            return false;
        }
    }
};

#endif // SERVICEMANAGER_H

