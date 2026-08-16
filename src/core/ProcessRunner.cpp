/**
 * @file ProcessRunner.cpp
 * @brief Implementation of ProcessRunner using QProcess.
 */

#include <QProcess>

#include "ProcessRunner.h"

int ProcessRunner::run(const QString &program,
                       const QStringList &arguments,
                       QString &output,
                       QString &errorOutput) const
{
    QProcess process;
    process.start(program, arguments);
    process.waitForFinished();

    output = process.readAllStandardOutput().trimmed();
    errorOutput = process.readAllStandardError().trimmed();
    return process.exitCode();
}
