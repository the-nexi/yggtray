/**
 * @file ServiceManager.cpp
 * @brief Implementation file for the ServiceManager class.
 *
 * Manages interactions with system services via systemctl commands.
 */

#include "ServiceManager.h"
#include <QDebug>

/**
 * @brief Checks if the service is currently running.
 * @return True if the service is running, false otherwise.
 */
bool ServiceManager::isServiceRunning() const {
    QString output, errorOutput;
    QStringList arguments = {"is-active", serviceName};
    int exitCode = processRunner->run("systemctl", arguments, output, errorOutput);
    Q_UNUSED(errorOutput);
    return (exitCode == 0 && output == "active");
}

/**
 * @brief Starts the specified service.
 * @return True if the service was started successfully, false otherwise.
 */
bool ServiceManager::startService() const {
    return executeCommand("start");
}

/**
 * @brief Stops the specified service.
 * @return True if the service was stopped successfully, false otherwise.
 */
bool ServiceManager::stopService() const {
    return executeCommand("stop");
}

/**
 * @brief Enables and starts the specified service immediately.
 * @return True if the service was enabled and started successfully, false otherwise.
 */
bool ServiceManager::enableService() const {
    return executeCommand("enable --now");
}

/**
 * @brief Executes a systemctl command for the specified action.
 * @param action The action to perform (e.g., "start", "stop", or "enable --now").
 * @return True if the command was successful, false otherwise.
 */
bool ServiceManager::executeCommand(const QString &action) const {
    QString output, errorOutput;
    QStringList arguments = action.split(' ');
    arguments << serviceName;
    int exitCode = processRunner->run("pkexec", QStringList() << "systemctl" << arguments, output, errorOutput);

    if (exitCode == 0) {
        qDebug() << action
                 << "command executed successfully for"
                 << serviceName;
        return true;
    } else {
        qDebug() << action
                 << "command failed for"
                 << serviceName << ":" << errorOutput;
        return false;
    }
}
