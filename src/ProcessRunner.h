/**
 * @file ProcessRunner.h
 * @brief Concrete implementation of IProcessRunner using QProcess.
 */

#ifndef PROCESSRUNNER_H
#define PROCESSRUNNER_H

#include "IProcessRunner.h"
#include <QObject>

/**
 * @class ProcessRunner
 * @brief Runs system commands using QProcess.
 */
class ProcessRunner : public QObject, public IProcessRunner {
    Q_OBJECT
public:
    ~ProcessRunner() override = default;

    /**
     * @brief Executes a command with arguments using QProcess.
     * @param program The program to execute.
     * @param arguments The list of arguments to pass to the program.
     * @param output The standard output from the command.
     * @param errorOutput The standard error from the command.
     * @return The exit code of the process.
     */
    int run(const QString &program,
            const QStringList &arguments,
            QString &output,
            QString &errorOutput) const override;
};

#endif // PROCESSRUNNER_H
