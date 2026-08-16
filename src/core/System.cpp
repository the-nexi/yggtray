/**
 * @file System.cpp
 * @brief Methods for interaction with the system.
 *
 * The "System" class contains methods for interaction with the operating
 * system.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "ProcessRunner.h"
#include "System.h"

/**
 * Check if the current user is included into the specified group.
 *
 * @param groupName The name of the group to check.
 * @return True if the current user is included in the group, false
 * otherwise.
 */
bool System::isUserInGroup(const QString &groupName) {
    ProcessRunner process;
    QString output, errorOutput;
    process.run("groups", QStringList{}, output, errorOutput);
    return output.split(" ").contains(groupName);
}

/**
 * Add the current user to the specified group.
 *
 * @param groupName The name of the group to use.
 * @return True if the current user is successfully added to the group,
 * false otherwise.
 */
bool System::addUserToGroup(const QString &groupName) {
    ProcessRunner process;
    QString output, errorOutput;
    QStringList arguments = {
        "usermod", "-a", "-G", groupName, qgetenv("USER")
    };
    // GUI password prompt
    int rc = process.run("pkexec", arguments, output, errorOutput);
    return (rc == 0);
}

/**
 * Detect distribution name for the platform OS.
 *
 * @return The distribution name or "unknown" if the distribution name
 * could not be detected.
 */
QString System::detectDistribution() {
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
    ProcessRunner process;
    QString output, errorOutput;
    int rc;
    QStringList args;

    args.clear();
    args << "-v" << "pacman";
    rc = process.run("command", args, output, errorOutput);
    if (rc == 0) {
        return "arch";
    }

    args.clear();
    args << "-v" << "apt-get";
    process.run("command", args, output, errorOutput);
    if (rc == 0) {
        return "debian";
    }

    args.clear();
    args << "-v" << "dnf";
    process.run("command", args, output, errorOutput);
    if (rc == 0) {
        return "fedora";
    }

    args.clear();
    args << "-v" << "zypper";
    process.run("command", args, output, errorOutput);
    if (rc == 0) {
        return "suse";
    }

    return "unknown";
}

/**
 * Create directory if it doesn't exist.
 *
 * @return True if directory exists or it is successfully created, false
 * otherwise.
 */
bool System::mkdir(const QString& path) {
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists()) {
        ProcessRunner process;
        QString output, errorOutput;
        int rc = process.run("pkexec", {"mkdir", "-p", dir.path()},
                             output, errorOutput);
        return (rc == 0);
    }
    return true;
}

/**
 * Run "which" command.
 *
 * @param args Command arguments.
 * @return True when the command executed successfully, false otherwise.
 */
bool System::which(const QStringList& args) {
    ProcessRunner process;
    QString output, errorOutput;
    int rc = process.run("which", args, output, errorOutput);
    return (rc == 0);
}
