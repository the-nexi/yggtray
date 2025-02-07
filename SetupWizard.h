/**
 * @file SetupWizard.h
 * @brief Enhanced setup wizard for Yggdrasil Tray with Freedesktop-compliant config property.
 * 
 * Handles automatic detection of group membership, allows users to perform
 * individual setup steps, and ensures the wizard only runs on first setup.
 */

#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <QString>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>
#include <QProcess>
#include <QMessageBox>
#include <QInputDialog>
#include "ServiceManager.h" // Include the ServiceManager header

/**
 * @class SetupWizard
 * @brief Enhanced setup wizard for first-time configuration tasks.
 */
class SetupWizard {
public:
    /**
     * @brief Runs the setup wizard if not already completed.
     * @param forceRun If true, the wizard runs regardless of the config file property.
     */
    void run(bool forceRun = false) {
        if (!forceRun && isSetupComplete()) {
            return; // Skip if setup is already complete and not triggered via CLI
        }

        // Perform the wizard actions
        if (!isUserInGroup("yggdrasil")) {
            QMessageBox::information(
                nullptr, 
                QObject::tr("Group Membership"),
                QObject::tr("You are not in the 'yggdrasil' group. To use this application, you must be added to this group.")
            );

            QString choice = promptAction(QObject::tr("Would you like to add yourself to the 'yggdrasil' group now?"),
                                          {QObject::tr("Add Me"), QObject::tr("Skip")});
            if (choice == QObject::tr("Add Me")) {
                addUserToGroup("yggdrasil");
            } else {
                QMessageBox::warning(
                    nullptr, 
                    QObject::tr("Setup Incomplete"),
                    QObject::tr("You need to be in the 'yggdrasil' group to use the application. Exiting setup.")
                );
                return;
            }
        }

        QString choice = promptAction(QObject::tr("Would you like to configure ip6tables for Yggdrasil?"), {QObject::tr("Configure"), QObject::tr("Skip")});
        if (choice == QObject::tr("Configure")) {
            configureIptables();
        }

        // Mark setup as complete
        markSetupComplete();
    }

private:
    QString getConfigFilePath() const {
        QString configDirPath = QDir::homePath() + "/.config/yggdrasil";
        return configDirPath + "/yggtray.conf";
    }

    /**
     * @brief Checks if the setup is marked as complete in the config file.
     * @return True if the setup is complete, false otherwise.
     */
    bool isSetupComplete() {
        QFile configFile(getConfigFilePath());
        if (!configFile.exists()) {
            return false;
        }

        if (!configFile.open(QFile::ReadOnly | QFile::Text)) {
            return false;
        }

        QTextStream in(&configFile);
        QString content = in.readAll();
        configFile.close();

        return content.contains("setup_complete=true");
    }

    /**
     * @brief Marks the setup as complete in the config file.
     */
    void markSetupComplete() {
        QString configFilePath = getConfigFilePath();
        QDir configDir(QFileInfo(configFilePath).absolutePath());
        if (!configDir.exists()) {
            configDir.mkpath(".");
        }

        QFile configFile(configFilePath);
        if (configFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&configFile);
            out << "setup_complete=true\n";
            configFile.close();
        } else {
            QMessageBox::warning(nullptr, QObject::tr("Setup Wizard"), QObject::tr("Failed to mark the setup as complete."));
        }
    }

    /**
     * @brief Displays a custom action prompt to the user.
     * @param message The question to ask the user.
     * @param options A list of string options for the user to choose from.
     * @return The selected option as a string.
     */
    QString promptAction(const QString &message, const QStringList &options) {
        bool ok;
        QString choice = QInputDialog::getItem(nullptr, QObject::tr("Setup Wizard"), message, options, 0, false, &ok);
        return ok ? choice : QString();
    }

    /**
     * @brief Checks if the current user is in a specific group.
     * @param groupName The name of the group to check.
     * @return True if the user is in the group, false otherwise.
     */
    bool isUserInGroup(const QString &groupName) {
        QProcess process;
        process.start("groups", QStringList{});
        process.waitForFinished();
        QString output = process.readAllStandardOutput().trimmed();
        return output.split(" ").contains(groupName);
    }

    /**
     * @brief Adds the current user to a specified group.
     * @param groupName The name of the group to add the user to.
     */
    void addUserToGroup(const QString &groupName) {
        QProcess process;
        QStringList arguments = {"usermod", "-a", "-G", groupName, qgetenv("USER")};
        process.start("pkexec", arguments); // GUI password prompt
        process.waitForFinished();

        if (process.exitCode() == 0) {
            QMessageBox::information(nullptr, QObject::tr("Group Addition"), 
                                     QString(QObject::tr("You have been added to the '%1' group. Please log out and log back in for the changes to take effect.")).arg(groupName));
        } else {
            QMessageBox::critical(nullptr, QObject::tr("Group Addition"), 
                                  QString(QObject::tr("Failed to add you to the '%1' group. Ensure you have the necessary permissions.")).arg(groupName));
        }
    }

    /**
     * @brief Configures ip6tables with predefined rules and manages existing configuration.
     */
    void configureIptables() {
        QString rulesFilePath = "/etc/iptables/ip6tables.rules";
        QString iptablesRules = 
R"(#yggdrasil
*filter
:INPUT ACCEPT [8:757]
:FORWARD ACCEPT [0:0]
:OUTPUT ACCEPT [5:463]
-A INPUT -i tun0 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
-A INPUT -i tun0 -m conntrack --ctstate INVALID -j DROP
-A INPUT -i tun0 -j DROP
COMMIT)";

        if (QFile::exists(rulesFilePath)) {
            QString choice = promptAction(QObject::tr("The ip6tables configuration file already exists. What would you like to do?"),
                                          {QObject::tr("Overwrite"), QObject::tr("Append"), QObject::tr("Cancel")});
            if (choice == QObject::tr("Overwrite")) {
                writeToFile(rulesFilePath, iptablesRules, false);
            } else if (choice == QObject::tr("Append")) {
                writeToFile(rulesFilePath, iptablesRules, true);
            } else {
                QMessageBox::information(nullptr, "ip6tables", QObject::tr("No changes were made to the ip6tables configuration."));
                return;
            }
        } else {
            writeToFile(rulesFilePath, iptablesRules, false);
        }

        enableIp6tablesService();
    }

    /**
     * @brief Writes rules to the ip6tables configuration file with elevated permissions.
     * @param filePath The path to the configuration file.
     * @param rules The ip6tables rules to write.
     * @param append If true, appends to the file; otherwise, overwrites it.
     */
    void writeToFile(const QString &filePath, const QString &rules, bool append) {
        QString tempFilePath = "/tmp/ip6tables_temp.rules";
        QFile tempFile(tempFilePath);
        if (tempFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&tempFile);
            out << rules << "\n";
            tempFile.close();
        } else {
            QMessageBox::critical(nullptr, "ip6tables", QObject::tr("Failed to create a temporary file for ip6tables rules."));
            return;
        }

        QString command = append
            ? QString("cat %1 | tee -a %2").arg(tempFilePath, filePath)
            : QString("cat %1 | tee %2").arg(tempFilePath, filePath);

        QProcess process;
        process.start("pkexec", {"bash", "-c", command});
        process.waitForFinished();

        if (process.exitCode() == 0) {
            QMessageBox::information(nullptr, "ip6tables", append ? QObject::tr("Rules have been appended to the configuration.") : QObject::tr("Rules have been written to the configuration."));
        } else {
            QMessageBox::critical(nullptr, "ip6tables", QObject::tr("Failed to write to the ip6tables configuration file. Ensure you have the necessary permissions."));
        }

        QFile::remove(tempFilePath); // Clean up the temporary file
    }

    /**
     * @brief Enables and starts the ip6tables service using ServiceManager.
     */
    void enableIp6tablesService() {
        ServiceManager serviceManager("ip6tables");
        if (serviceManager.enableService()) {
            QMessageBox::information(nullptr, QObject::tr("ip6tables Service"), QObject::tr("The ip6tables service has been enabled and started successfully."));
        } else {
            QMessageBox::critical(nullptr, QObject::tr("ip6tables Service"), QObject::tr("Failed to enable and start the ip6tables service. Ensure it is properly installed."));
        }
    }
};

#endif // SETUPWIZARD_H

