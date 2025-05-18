/**
 * @file MockProcessRunner.h
 * @brief Mock implementation of IProcessRunner for unit testing.
 */

#ifndef MOCKPROCESSRUNNER_H
#define MOCKPROCESSRUNNER_H

#include "IProcessRunner.h"
#include <QString>
#include <QStringList>
#include <map>
#include <tuple>

/**
 * @class MockProcessRunner
 * @brief Mocks system command execution for testing ServiceManager.
 */
class MockProcessRunner : public IProcessRunner {
public:
    struct Call {
        QString program;
        QStringList arguments;
    };

    mutable std::vector<Call> calls;
    std::map<std::tuple<QString, QStringList>, std::tuple<int, QString, QString>> responses;

    void setResponse(const QString &program, const QStringList &arguments, int exitCode, const QString &output, const QString &errorOutput) {
        responses[std::make_tuple(program, arguments)] = std::make_tuple(exitCode, output, errorOutput);
    }

    int run(const QString &program,
            const QStringList &arguments,
            QString &output,
            QString &errorOutput) const override
    {
        calls.push_back({program, arguments});
        auto key = std::make_tuple(program, arguments);
        if (responses.count(key)) {
            auto resp = responses.at(key);
            output = std::get<1>(resp);
            errorOutput = std::get<2>(resp);
            return std::get<0>(resp);
        }
        output.clear();
        errorOutput = "Mock: No response set";
        return 1;
    }
};

#endif // MOCKPROCESSRUNNER_H
