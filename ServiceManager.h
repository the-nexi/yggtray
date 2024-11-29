/**
 * @file ServiceManager.h
 * @brief Header file for the ServiceManager class.
 * 
 * Manages interactions with system services via systemctl commands.
 */

#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include <QString>
#include <QProcess>
#include <QDebug>

/**
 * @class ServiceManager
 * @brief Handles system service management using systemctl commands.
 * 
 * This class provides methods to check the status of a service, start
 * a service, and stop a service using system commands.
 */
class ServiceManager {
public:
    /**
     * @brief Constructs a ServiceManager for the specified service.
     * @param serviceName The name of the service to manage.
     */
    explicit ServiceManager(const QString &serviceName) : serviceName(serviceName) {}

    /**
     * @brief Checks if the service is currently running.
     * @return True if the service is running, false otherwise.
     * 
     * Uses the `systemctl is-active` command to determine the status
     * of the specified service.
     */
    bool isServiceRunning() const {
        QProcess process;
        QStringList arguments = {"is-active", serviceName};
        process.start("systemctl", arguments);
        process.waitForFinished();

        QString output = process.readAllStandardOutput().trimmed();
        return (output == "active");
    }

    /**
     * @brief Starts the specified service.
     * @return True if the service was started successfully, false otherwise.
     * 
     * Executes the `systemctl start` command using pkexec.
     */
    bool startService() const {
        return executeCommand("start");
    }

    /**
     * @brief Stops the specified service.
     * @return True if the service was stopped successfully, false otherwise.
     * 
     * Executes the `systemctl stop` command using pkexec.
     */
    bool stopService() const {
        return executeCommand("stop");
    }

private:
    QString serviceName; ///< The name of the service to manage.

    /**
     * @brief Executes a systemctl command for the specified action.
     * @param action The action to perform (e.g., "start" or "stop").
     * @return True if the command was successful, false otherwise.
     * 
     * This method uses pkexec to run the systemctl command with
     * elevated privileges.
     */
    bool executeCommand(const QString &action) const {
        QProcess process;
        QStringList arguments = {"systemctl", action, serviceName};
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

