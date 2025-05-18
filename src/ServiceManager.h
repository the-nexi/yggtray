/**
 * @file ServiceManager.h
 * @brief Header file for the ServiceManager class.
 *
 * Manages interactions with system services via systemctl commands.
 */

#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include <QString>

/**
 * @class ServiceManager
 * @brief Handles system service management using systemctl commands.
 *
 * This class provides methods to check the status of a service, start
 * a service, stop a service, and enable a service using system commands.
 */
#include "IProcessRunner.h"

class ServiceManager {
public:
    /**
     * @brief Constructs a ServiceManager for the specified service.
     * @param serviceName The name of the service to manage.
     * @param processRunner Pointer to an IProcessRunner implementation.
     */
    ServiceManager(const QString &serviceName, const IProcessRunner *processRunner)
        : serviceName(serviceName), processRunner(processRunner) {}

    /**
     * @brief Checks if the service is currently running.
     * @return True if the service is running, false otherwise.
     *
     * Uses the `systemctl is-active` command to determine the status
     * of the specified service.
     */
    bool isServiceRunning() const;

    /**
     * @brief Starts the specified service.
     * @return True if the service was started successfully, false otherwise.
     *
     * Executes the `systemctl start` command using pkexec.
     */
    bool startService() const;

    /**
     * @brief Stops the specified service.
     * @return True if the service was stopped successfully, false otherwise.
     *
     * Executes the `systemctl stop` command using pkexec.
     */
    bool stopService() const;

    /**
     * @brief Enables and starts the specified service immediately.
     * @return True if the service was enabled and started successfully, false
     * otherwise.
     *
     * Executes the `systemctl enable --now` command using pkexec.
     */
    bool enableService() const;

private:
    QString serviceName; ///< The name of the service to manage.
    const IProcessRunner *processRunner; ///< Pointer to process runner.

    /**
     * @brief Executes a systemctl command for the specified action.
     * @param action The action to perform (e.g., "start", "stop", or "enable --now").
     * @return True if the command was successful, false otherwise.
     *
     * This method uses pkexec to run the systemctl command with
     * elevated privileges.
     */
    bool executeCommand(const QString &action) const;
};

#endif // SERVICEMANAGER_H
