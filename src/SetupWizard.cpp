#include <QDebug>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QTranslator>

#include "SetupWizard.h"

/**
 * Migrate old settings to QSettings.  Delete the old settings file.
 * @return true if the migration is done, false otherwise.
 */
bool SetupWizard::migrateSettings() {
    QFile configFile(getConfigFilePath());
    if (! configFile.exists()) {
        qDebug() << "No migration is needed.";
        return false;
    }

    if (! configFile.open(QFile::ReadOnly | QFile::Text)) {
        qCritical() << "Cannot open the old settings file:"
                    << configFile.fileName();
        QMessageBox::critical(
            nullptr,
            QObject::tr("Settings migration error"),
            QObject::tr("Cannot open the old settings file: ")
            + configFile.fileName());
        return false;
    }
    qDebug() << "Settings file migration:"
             << configFile.fileName() << " -> " << settings->fileName();
    QTextStream in(&configFile);
    QString content = in.readAll();
    configFile.close();

    settings->setValue("setup_wizard/setup_complete",
                       content.contains("setup_complete=true"));
    configFile.remove();
    qDebug() << "Settings file migration is finished.";
    return true;
}

void SetupWizard::run(bool forceRun) {

    // TODO: Remove this migration in the following Yggtray versions.
    migrateSettings();

    if (!forceRun && isSetupComplete()) {
        return; // Skip if setup is already complete and not triggered via CLI
    }

    // Perform the wizard actions
    if (!isUserInGroup("yggdrasil")) {
        QMessageBox::information(
            nullptr,
            QObject::tr("Group Membership"),
            QObject::tr(
                "You are not in the 'yggdrasil' group."
                " To use this application, you must be added to this group."));

        QString choice
            = promptAction(
                QObject::tr(
                    "Would you like to add yourself"
                    " to the 'yggdrasil' group now?"),
                { QObject::tr("Add Me"), QObject::tr("Skip") });
        if (choice == QObject::tr("Add Me")) {
            addUserToGroup("yggdrasil");
        } else {
            QMessageBox::warning(
                nullptr,
                QObject::tr("Setup Incomplete"),
                QObject::tr("You need to be in the 'yggdrasil' group"
                            " to use the application. Exiting setup.")
                );
            return;
        }
    }

    QString choice = promptAction(
        QObject::tr("Would you like to configure ip6tables for Yggdrasil?"),
        {QObject::tr("Configure"), QObject::tr("Skip")}
        );
    if (choice == QObject::tr("Configure")) {
        configureIptables();
    }

    // Ensure Yggdrasil main config file exists
    ensureYggdrasilConfigExists();

    // Mark setup as complete
    markSetupComplete();
}

void SetupWizard::ensureYggdrasilConfigExists() {
    QString path1 = "/etc/yggdrasil/yggdrasil.conf";
    QString path2 = "/etc/yggdrasil.conf";
    QString targetPath;

    if (QFile::exists(path1)) {
        // qDebug() << "Yggdrasil config found at" << path1;
        return;
    }
    if (QFile::exists(path2)) {
        // qDebug() << "Yggdrasil config found at" << path2;
        return;
    }

    // Neither exists, determine where to create it
    QDir yggdrasilDir("/etc/yggdrasil");
    if (yggdrasilDir.exists()) {
        targetPath = path1;
    } else {
        targetPath = path2;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(
        nullptr,
        QObject::tr("Yggdrasil Configuration"),
        QString(QObject::tr("The Yggdrasil configuration file (%1) was not found. "
                            "Would you like to generate it now using 'yggdrasil -genconf'?"))
        .arg(targetPath),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QProcess process;
        QStringList args;

        // Ensure parent directory exists if needed
        QFileInfo fileInfo(targetPath);
        QDir parentDir = fileInfo.dir();
        if (targetPath.startsWith("/etc/yggdrasil/") && !parentDir.exists()) {
            args << "mkdir" << "-p" << parentDir.absolutePath();
            QProcess mkdirProcess;
            mkdirProcess.start("pkexec", args);
            mkdirProcess.waitForFinished(-1); // Wait for mkdir to complete
            if (mkdirProcess.exitCode() != 0) {
                QMessageBox::warning(
                    nullptr,
                    QObject::tr("Yggdrasil Configuration"),
                    QString(
                        QObject::tr("Failed to create directory %1. "
                                    "Error: %2. Exit code: %3"))
                    .arg(parentDir.absolutePath(),
                         mkdirProcess.errorString(),
                         QString::number(mkdirProcess.exitCode())));
                return;
            }
        }

        // Command to generate the config file
        // Example:
        //   pkexec bash -c "yggdrasil -genconf > '/etc/yggdrasil/yggdrasil.conf'"
        QString command
            = QString("yggdrasil -genconf > '%1'").arg(targetPath);

        args.clear();
        args << "bash" << "-c" << command;

        process.start("pkexec", args);
        process.waitForFinished(-1); // Wait indefinitely

        if (process.exitCode() == 0 && QFile::exists(targetPath)) {
            // Additionally, check if the file is not empty, as genconf
            // should produce content.
            QFile genFile(targetPath);
            if (genFile.open(QIODevice::ReadOnly)) {
                bool isEmpty = genFile.size() == 0;
                genFile.close();
                if (isEmpty) {
                    // TODO: Simplify the code to make it more concise.
                    QMessageBox::warning(
                        nullptr,
                        QObject::tr("Yggdrasil Configuration"),
                        QString(QObject::tr(
                                    "Yggdrasil configuration file was created at %1, but it is empty."
                                    " 'yggdrasil -genconf' might have failed silently or yggdrasil command is not in PATH for root."))
                        .arg(targetPath));
                } else {
                    QMessageBox::information(
                        nullptr,
                        QObject::tr("Yggdrasil Configuration"),
                        QString(QObject::tr(
                                    "Yggdrasil configuration file generated successfully at %1."))
                        .arg(targetPath));
                }
            } else {
                QMessageBox::warning(
                    nullptr,
                    QObject::tr("Yggdrasil Configuration"),
                    QString(QObject::tr("Yggdrasil configuration file was created at %1, but could not be read to verify content."))
                    .arg(targetPath));
            }
        } else {
            QMessageBox::warning(
                nullptr,
                QObject::tr("Yggdrasil Configuration"),
                QString(QObject::tr("Failed to generate Yggdrasil configuration file at %1. "
                                    "Command was: pkexec bash -c \"%2\". Error: %3. Exit code: %4. "
                                    "Ensure 'yggdrasil' is in the system PATH and pkexec is configured."))
                .arg(targetPath, command, process.errorString(), QString::number(process.exitCode())));
        }
    }
}

QString SetupWizard::getConfigFilePath() const {
    QString configDirPath = QDir::homePath() + "/.config/yggdrasil";
    return configDirPath + "/yggtray.conf";
}

bool SetupWizard::isSetupComplete() {
    return settings->value("setup_wizard/setup_complete",
                           false).toBool();
}

void SetupWizard::markSetupComplete() {
    settings->setValue("setup_wizard/setup_complete", true);
}

QString SetupWizard::promptAction(const QString &message,
                                  const QStringList &options) {
    bool ok;
    QString choice = QInputDialog::getItem(
        nullptr,
        QObject::tr("Setup Wizard"),
        message,
        options,
        0,
        false,
        &ok);
    return ok ? choice : QString();
}

bool SetupWizard::isUserInGroup(const QString &groupName) {
    QProcess process;
    process.start("groups", QStringList{});
    process.waitForFinished();
    QString output = process.readAllStandardOutput().trimmed();
    return output.split(" ").contains(groupName);
}

void SetupWizard::addUserToGroup(const QString &groupName) {
    QProcess process;
    QStringList arguments = {"usermod", "-a", "-G", groupName, qgetenv("USER")};
    process.start("pkexec", arguments); // GUI password prompt
    process.waitForFinished();

    if (process.exitCode() == 0) {
        QMessageBox::information(
            nullptr,
            QObject::tr("Group Addition"),
            QString(QObject::tr(
                        "You have been added to the '%1' group. "
                        "Please log out and log back in for the changes to take effect."))
            .arg(groupName)
            );
    } else {
        QMessageBox::critical(
            nullptr,
            QObject::tr("Group Addition"),
            QString(QObject::tr(
                        "Failed to add you to the '%1' group. "
                        "Ensure you have the necessary permissions."))
            .arg(groupName)
            );
    }
}

QString SetupWizard::detectDistribution() {
    // Try os-release first (most modern distros)
    QFile osRelease("/etc/os-release");
    if (osRelease.open(QFile::ReadOnly | QFile::Text)) {
        QString content = osRelease.readAll();
        osRelease.close();

        // Check for common distro identifiers
        if (content.contains("ID=arch")
            || content.contains("ID=endeavouros")
            || content.contains("ID=manjaro"))
            return "arch";
        if (content.contains("ID=ubuntu")
            || content.contains("ID=debian")
            || content.contains("ID=linuxmint"))
            return "debian";
        if (content.contains("ID=fedora"))
            return "fedora";
        if (content.contains("ID=opensuse"))
            return "suse";
    }

    // Fallback to command tools
    QProcess process;
    QStringList args;

    args.clear();
    args << "-v" << "pacman";
    process.start("command", args);
    process.waitForFinished();
    if (process.exitCode() == 0)
        return "arch";

    args.clear();
    args << "-v" << "apt-get";
    process.start("command", args);
    process.waitForFinished();
    if (process.exitCode() == 0)
        return "debian";

    args.clear();
    args << "-v" << "dnf";
    process.start("command", args);
    process.waitForFinished();
    if (process.exitCode() == 0)
        return "fedora";

    args.clear();
    args << "-v" << "zypper";
    process.start("command", args);
    process.waitForFinished();
    if (process.exitCode() == 0)
        return "suse";

    return "unknown";
}

SetupWizard::DistroInfo SetupWizard::getDistroInfo() {
    QString distro = detectDistribution();
    SetupWizard::DistroInfo info;

    // Distribution-specific configuration
    if (distro == "debian") {
        info.rulesPath = "/etc/iptables/rules.v6";
        info.serviceName = "netfilter-persistent";
        info.packageManager = "apt-get";
        info.packageName = "iptables-persistent";
        info.installCmd = "apt-get install -y iptables-persistent";
    }
    else if (distro == "fedora") {
        info.rulesPath = "/etc/sysconfig/ip6tables";
        info.serviceName = "ip6tables";
        info.packageManager = "dnf";
        info.packageName = "iptables-services";
        info.installCmd = "dnf install -y iptables-services";
    }
    else if (distro == "suse") {
        info.rulesPath = "/etc/sysconfig/ip6tables";
        info.serviceName = "ip6tables";
        info.packageManager = "zypper";
        info.packageName = "iptables";
        info.installCmd = "zypper install -y iptables";
    }
    else {
        // Default for Arch and others
        info.rulesPath = "/etc/iptables/ip6tables.rules";
        info.serviceName = "ip6tables";
        info.packageManager = "pacman";
        info.packageName = "iptables";
        info.installCmd = "pacman -S --noconfirm iptables";
    }

    return info;
}

bool SetupWizard::isPackageInstalled(const QString &packageName,
                                     const QString &packageManager) {
    QProcess process;
    QStringList args;

    if (packageManager == "pacman") {
        args.clear();
        args << "-Q" << packageName;
        process.start(packageManager, args);
        process.waitForFinished();
        return (process.exitCode() == 0);
    }
    else if (packageManager == "apt-get") {
        // For Ubuntu/Debian, use dpkg directly which is more reliable
        args.clear();
        args << "-s" << packageName;
        process.start("dpkg", args);
        process.waitForFinished();

        if (process.exitCode() == 0) {
            QString output = process.readAllStandardOutput();
            // Look for "Status: install ok installed" in the output
            return output.contains("Status: install ok installed");
        }
        return false;
    }
    else if (packageManager == "dnf") {
        args.clear();
        args << "list" << "installed" << packageName;
        process.start(packageManager, args);
        process.waitForFinished();
        return (process.exitCode() == 0);
    }
    else if (packageManager == "zypper") {
        args.clear();
        args << "se" << "-i" << packageName;
        process.start(packageManager, args);
        process.waitForFinished();
        return (process.exitCode() == 0);
    }

    return false;  // Unknown package manager
}

bool SetupWizard::ensurePackageInstalled(const DistroInfo &info) {
    // Check if package is already installed
    if (isPackageInstalled(info.packageName, info.packageManager)) {
        return true;
    }

    // Prompt user to install the package
    QString message
        = QString(QObject::tr(
                      "The package '%1' is required for ip6tables configuration but is not installed. Would you like to install it now?"))
        .arg(info.packageName);

    QStringList options;
    options << QObject::tr("Install") << QObject::tr("Skip");
    QString choice = promptAction(message, options);

    if (choice == QObject::tr("Install")) {
        // Use x-terminal-emulator on Debian/Ubuntu, or fallback to xterm
        QProcess termDetectProcess;
        QString terminalCmd;

        // Try to detect the default terminal emulator for this system
        if (info.packageManager == "apt-get") {
            // Debian/Ubuntu specific - check if x-terminal-emulator exists
            termDetectProcess.start("which", QStringList() << "x-terminal-emulator");
            termDetectProcess.waitForFinished();
            if (termDetectProcess.exitCode() == 0) {
                terminalCmd = "x-terminal-emulator -e";
            }
        }

        // Try common terminals if we don't have a specific one yet
        if (terminalCmd.isEmpty()) {
            QStringList terminals = {
                "konsole",
                "gnome-terminal",
                "xfce4-terminal",
                "mate-terminal",
                "xterm"
            };
            for (const auto& term : terminals) {
                termDetectProcess.start("which", QStringList() << term);
                termDetectProcess.waitForFinished();
                if (termDetectProcess.exitCode() == 0) {
                    if (term == "gnome-terminal") {
                        terminalCmd = QString("%1 -- ").arg(term);
                    } else if (term == "konsole") {
                        terminalCmd = QString("%1 -e ").arg(term);
                    } else {
                        terminalCmd = QString("%1 -e ").arg(term);
                    }
                    break;
                }
            }
        }

        // If we couldn't find a terminal, fall back to xterm as
        // a last resort
        if (terminalCmd.isEmpty()) {
            terminalCmd = "xterm -e";
        }

        // Create the full command with an appropriate pause at the end
        QString fullCmd;
        if (info.packageManager == "apt-get" || info.packageManager == "dnf") {
            // These package managers need sudo
            fullCmd = QString(
                "%1 bash -c \"echo 'Installing %2 package...'; sudo %3; echo 'Press Enter to close this window.'; read\"")
                .arg(terminalCmd, info.packageName, info.installCmd);
        } else {
            // Others like pacman need to be run directly with sudo
            fullCmd = QString(
                "%1 bash -c \"echo 'Installing %2 package...'; sudo %3; echo 'Press Enter to close this window.'; read\"")
                .arg(terminalCmd, info.packageName, info.installCmd);
        }

        // Run the terminal with installation command
        QProcess process;
        process.startDetached(fullCmd);

        // Give some time for package installation
        QMessageBox msgBox;
        msgBox.setWindowTitle(QObject::tr("Package Installation"));
        msgBox.setText(
            QObject::tr(
                "The package installation window has been opened.\n\n"
                "Please complete the installation in the terminal window and then click OK to continue."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();

        // Verify installation after user confirms
        if (isPackageInstalled(info.packageName, info.packageManager)) {
            QMessageBox::information(
                nullptr,
                QObject::tr("Package Installation"),
                QString(QObject::tr(
                            "The package '%1' has been successfully installed."))
                .arg(info.packageName)
                );
            return true;
        } else {
            QMessageBox::critical(
                nullptr,
                QObject::tr("Package Installation"),
                QString(QObject::tr(
                            "Failed to install package '%1' or the installation could not be verified. "
                            "You may need to install it manually.")).arg(info.packageName)
                );
            return false;
        }
    }

    return false;
}

bool SetupWizard::ensureNetfilterPersistent() {
    QString distro = detectDistribution();

    // Only needed for Debian-based distributions
    if (distro == "debian") {
        // Use dpkg to properly check if the package is installed
        QProcess process;
        QStringList args;
        args.clear();
        args << "-s" << "netfilter-persistent";
        process.start("dpkg", args);
        process.waitForFinished();

        bool isInstalled = false;
        if (process.exitCode() == 0) {
            QString output = process.readAllStandardOutput();
            isInstalled = output.contains("Status: install ok installed");
        }

        // If netfilter-persistent is not found
        if (!isInstalled) {
            QString message
                = QObject::tr(
                    "The 'netfilter-persistent' package is required for ip6tables "
                    "configuration on this system but is not installed. "
                    "Would you like to install it now?");

            QStringList options;
            options << QObject::tr("Install") << QObject::tr("Skip");
            QString choice = promptAction(message, options);

            if (choice == QObject::tr("Install")) {
                // Detect terminal similar to ensurePackageInstalled
                QProcess termDetectProcess;
                QString terminalCmd;

                termDetectProcess.start("which", QStringList() << "x-terminal-emulator");
                termDetectProcess.waitForFinished();
                if (termDetectProcess.exitCode() == 0) {
                    terminalCmd = "x-terminal-emulator -e";
                } else {
                    QStringList terminals = {
                        "konsole",
                        "gnome-terminal",
                        "xfce4-terminal",
                        "mate-terminal",
                        "xterm"
                    };
                    for (const auto& term : terminals) {
                        termDetectProcess.start("which", QStringList() << term);
                        termDetectProcess.waitForFinished();
                        if (termDetectProcess.exitCode() == 0) {
                            if (term == "gnome-terminal") {
                                terminalCmd = QString("%1 -- ").arg(term);
                            } else {
                                terminalCmd = QString("%1 -e ").arg(term);
                            }
                            break;
                        }
                    }
                    if (terminalCmd.isEmpty()) terminalCmd = "xterm -e";
                }

                QString fullCmd
                    = QString(
                        "%1 bash -c \"echo 'Installing netfilter-persistent package...'; "
                        "sudo apt-get install -y netfilter-persistent; "
                        "echo 'Press Enter to close this window.'; read\"")
                    .arg(terminalCmd);

                // Open terminal for installation
                QProcess installProcess;
                installProcess.startDetached(fullCmd);

                // Wait for user to complete installation
                QMessageBox msgBox;
                msgBox.setWindowTitle(QObject::tr("Package Installation"));
                msgBox.setText(
                    QObject::tr(
                        "The netfilter-persistent installation window has been opened.\n\n"
                        "Please complete the installation in the terminal window and then click OK to continue."));
                msgBox.setStandardButtons(QMessageBox::Ok);
                msgBox.exec();

                // Verify installation using dpkg
                process.start("dpkg", args);
                process.waitForFinished();
                if (process.exitCode() == 0) {
                    QString output = process.readAllStandardOutput();
                    return output.contains("Status: install ok installed");
                }
                return false;
            }
            return false;
        }
        return true;
    }

    // Not needed for non-Debian systems
    return true;
}

void SetupWizard::configureIptables() {
    DistroInfo distroInfo = getDistroInfo();

    // Create directory if it doesn't exist
    QDir dir = QFileInfo(distroInfo.rulesPath).dir();
    if (!dir.exists()) {
        QProcess process;
        process.start("pkexec", {"mkdir", "-p", dir.path()});
        process.waitForFinished();
    }

    // For Debian-based systems, specifically check for netfilter-persistent
    if ((distroInfo.serviceName == "netfilter-persistent")
        && (! ensureNetfilterPersistent())) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("ip6tables Configuration"),
            QObject::tr(
                "Cannot configure ip6tables without the netfilter-persistent package.")
            );
        return;
    }

    // Make sure the required package is installed
    if (!ensurePackageInstalled(distroInfo)) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("ip6tables Configuration"),
            QObject::tr("Cannot configure ip6tables without the required package.")
            );
        return;
    }

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

    if (QFile::exists(distroInfo.rulesPath)) {
        QString choice = promptAction(
            QObject::tr("The ip6tables configuration file already exists. "
                        "What would you like to do?"),
            { QObject::tr("Overwrite"),
              QObject::tr("Append"),
              QObject::tr("Don't change the configuration file") });
        if (choice == QObject::tr("Overwrite")) {
            writeToFile(distroInfo.rulesPath, iptablesRules, false);
        } else if (choice == QObject::tr("Append")) {
            writeToFile(distroInfo.rulesPath, iptablesRules, true);
        } else {
            QMessageBox::information(
                nullptr,
                "ip6tables",
                QObject::tr(
                    "No changes were made to the ip6tables configuration."));
            return;
        }
    } else {
        writeToFile(distroInfo.rulesPath, iptablesRules, false);
    }

    enableIp6tablesService(distroInfo);
}

void SetupWizard::writeToFile(const QString &filePath,
                              const QString &rules,
                              bool append) {
    QString tempFilePath = "/tmp/ip6tables_temp.rules";
    QFile tempFile(tempFilePath);
    if (tempFile.open(QFile::WriteOnly | QFile::Text)) {
        QTextStream out(&tempFile);
        out << rules << "\n";
        tempFile.close();
    } else {
        QMessageBox::critical(
            nullptr,
            "ip6tables",
            QObject::tr("Failed to create a temporary file for ip6tables rules."));
        return;
    }

    QString command = append
        ? QString("cat %1 | tee -a %2").arg(tempFilePath, filePath)
        : QString("cat %1 | tee %2").arg(tempFilePath, filePath);

    QProcess process;
    QStringList args;
    args << "bash" << "-c" << command;
    process.start("pkexec", args);
    process.waitForFinished();

    if (process.exitCode() == 0) {
        QMessageBox::information(
            nullptr,
            "ip6tables",
            append ? QObject::tr("Rules have been appended to the configuration.")
            : QObject::tr("Rules have been written to the configuration."));
    } else {
        QMessageBox::critical(
            nullptr,
            "ip6tables",
            QObject::tr("Failed to write to the ip6tables configuration file. Ensure you have the necessary permissions."));
    }

    QFile::remove(tempFilePath); // Clean up the temporary file
}

void SetupWizard::enableIp6tablesService(const DistroInfo &distroInfo) {
    ProcessRunner processRunner;
    ServiceManager serviceManager(distroInfo.serviceName, &processRunner);
    if (serviceManager.enableService()) {
        QMessageBox::information(
            nullptr,
            QObject::tr("ip6tables Service"),
            QObject::tr("The ip6tables service has been enabled "
                        "and started successfully.")
            );
    } else {
        QString message;
        if (distroInfo.serviceName == "netfilter-persistent") {
            message
                = QObject::tr(
                    "Failed to enable the netfilter-persistent service. "
                    "Try running 'sudo netfilter-persistent save' and 'sudo netfilter-persistent reload' manually.");
        } else {
            message
                = QObject::tr(
                    "Failed to enable and start the %1 service. "
                    "Ensure it is properly installed.").arg(distroInfo.serviceName);
        }

        QMessageBox::critical(
            nullptr,
            QObject::tr("ip6tables Service"),
            message
            );
    }
}
