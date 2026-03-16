/**
 * @file SetupWizard.h
 * @brief Enhanced setup wizard for Yggdrasil Tray with
 * Freedesktop-compliant config property.
 *
 * Handles automatic detection of group membership, allows users to perform
 * individual setup steps, and ensures the wizard only runs on first setup.
 */

#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <memory>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QTranslator>

#include "ProcessRunner.h"
#include "ServiceManager.h"

/**
 * @class SetupWizard
 * @brief Enhanced setup wizard for first-time configuration tasks.
 */
class SetupWizard {
public:
    // Linux distribution information
    struct DistroInfo {
        QString rulesPath;       // Path to ip6tables rules file
        QString serviceName;     // Name of the ip6tables service
        QString packageManager;  // Package manager command
        QString packageName;     // Name of the required package
        QString installCmd;      // Full installation command
    };

    SetupWizard(std::shared_ptr<QSettings> settings)
        : settings(settings) {
        // Do nothing.
    }

    /**
     * @brief Runs the setup wizard if not already completed.
     * @param forceRun If true, the wizard runs regardless of the config file
     * property.
     */
    void run(bool forceRun = false);

private:
    std::shared_ptr<QSettings> settings;

    /**
     * @brief Ensures the main Yggdrasil configuration file
     * (yggdrasil.conf) exists.
     *
     * Checks common locations ("/etc/yggdrasil/yggdrasil.conf" or
     * "/etc/yggdrasil.conf") and creates an empty file if it's missing, using
     * pkexec for permissions.
     */
    void ensureYggdrasilConfigExists();

    QString getConfigFilePath() const;

    /**
     * @brief Checks if the setup is marked as complete in the config file.
     * @return True if the setup is complete, false otherwise.
     */
    bool isSetupComplete();

    /**
     * @brief Marks the setup as complete in the config file.
     */
    void markSetupComplete();

    /**
     * @brief Displays a custom action prompt to the user.
     * @param message The question to ask the user.
     * @param options A list of string options for the user to choose from.
     * @return The selected option as a string.
     */
    QString promptAction(const QString &message, const QStringList &options);

    /**
     * @brief Checks if the current user is in a specific group.
     * @param groupName The name of the group to check.
     * @return True if the user is in the group, false otherwise.
     */
    bool isUserInGroup(const QString &groupName);

    /**
     * @brief Adds the current user to a specified group.
     * @param groupName The name of the group to add the user to.
     */
    void addUserToGroup(const QString &groupName);

    /**
     * @brief Detects the Linux distribution currently running
     * @return The detected distribution ID string
     */
    QString detectDistribution();

    /**
     * @brief Gets information about ip6tables for the current distribution
     * @return A DistroInfo struct with paths and commands for
     * the current distribution
     */
    DistroInfo getDistroInfo();

    /**
     * @brief Checks if a package is installed
     * @param packageName The package name to check
     * @param packageManager The package manager to use
     * @return True if the package is installed
     */
    bool isPackageInstalled(const QString &packageName,
                            const QString &packageManager);

    /**
     * @brief Installs a package if necessary
     * @param info The DistroInfo containing package information
     * @return True if the package is installed (or was already installed)
     */
    bool ensurePackageInstalled(const DistroInfo &info);

    /**
     * @brief Specifically check and install netfilter-persistent on
     * Debian-based systems
     * @return True if netfilter-persistent is installed or not needed
     */
    bool ensureNetfilterPersistent();

    /**
     * @brief Configures ip6tables with predefined rules and manages existing configuration.
     */
    void configureIptables();

    /**
     * @brief Writes rules to the ip6tables configuration file with
     * elevated permissions.
     * @param filePath The path to the configuration file.
     * @param rules The ip6tables rules to write.
     * @param append If true, appends to the file; otherwise, overwrites it.
     */
    void writeToFile(const QString &filePath,
                     const QString &rules,
                     bool append);

    /**
     * @brief Enables and starts the ip6tables service using the appropriate service name.
     * @param distroInfo Distribution information containing service name
     */
    void enableIp6tablesService(const DistroInfo &distroInfo);
};

#endif // SETUPWIZARD_H
