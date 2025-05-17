/**
 * @file IProcessRunner.h
 * @brief Abstract interface for running system commands.
 *
 * Provides an interface for executing system commands, allowing for
 * mocking in unit tests.
 */

#ifndef IPROCESSRUNNER_H
#define IPROCESSRUNNER_H

#include <QString>
#include <QStringList>

/**
 * @class IProcessRunner
 * @brief Interface for running system commands.
 */
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;

    /**
     * @brief Executes a command with arguments.
     * @param program The program to execute (e.g., "systemctl").
     * @param arguments The list of arguments to pass to the program.
     * @param output The standard output from the command.
     * @param errorOutput The standard error from the command.
     * @return The exit code of the process.
     */
    virtual int run(const QString &program,
                    const QStringList &arguments,
                    QString &output,
                    QString &errorOutput) const = 0;
};

#endif // IPROCESSRUNNER_H
