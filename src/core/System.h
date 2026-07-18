#ifndef SYSTEM_H
#define SYSTEM_H

#include <QString>

/**
 * This class contains methods for interaction with the operating system.
 */
class System {
public:
    static bool isUserInGroup(const QString &groupName);
    static bool addUserToGroup(const QString &groupName);
    static QString detectDistribution();
    static bool mkdir(const QString& path);
};

#endif /* ifndef PLATFORM_H */
