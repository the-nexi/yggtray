/**
 * @file SocketManager.cpp
 * @brief Implementation file for the SocketManager class.
 *
 * Manages communication with a UNIX domain socket.
 */

#include "SocketManager.h"
#include <QJsonDocument>
#include <QFile>
#include <QDebug>

/**
 * @brief Constructs a SocketManager with multiple possible socket paths.
 * @param possibleSocketPaths A list of potential UNIX domain socket paths.
 */
SocketManager::SocketManager(const QStringList &possibleSocketPaths)
    : socketPaths(possibleSocketPaths), activeSocketPath("") {
    determineSocketPath();
}

/**
 * @brief Sends a request to the socket and receives the response.
 * @param request A QJsonObject containing the request data.
 * @return A QJsonObject containing the response, or an empty object if an error occurred.
 */
QJsonObject SocketManager::sendRequest(const QJsonObject &request) {
    if (activeSocketPath.isEmpty()) {
        qDebug() << "No valid socket path found.";
        return {};
    }

    QLocalSocket socket;
    socket.connectToServer(activeSocketPath);

    if (!socket.waitForConnected(3000)) {
        qDebug() << "Failed to connect to socket at" << activeSocketPath;
        return {};
    }

    QByteArray requestData =
        QJsonDocument(request).toJson(QJsonDocument::Compact)
        + "\n";
    socket.write(requestData);
    if (!socket.waitForBytesWritten(3000)) {
        qDebug() << "Failed to write request to socket";
        return {};
    }

    if (!socket.waitForReadyRead(3000)) {
        qDebug() << "No response from socket";
        return {};
    }

    QByteArray responseData = socket.readAll();
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
    if (!responseDoc.isObject()) {
        qDebug() << "Invalid JSON response from socket";
        return {};
    }

    return responseDoc.object();
}

/**
 * @brief Retrieves the Yggdrasil IP address from the socket.
 *
 * @return A QString containing the IP address, or "Unknown" if an error
 * occurred.
 */
QString SocketManager::getYggdrasilIP() {
    QJsonObject response = sendRequest({{"request", "getself"}});
    if (response.contains("response") && response["response"].isObject()) {
        return response["response"].toObject()["address"].toString();
    }
    return "Unknown";
}

/**
 * @brief Determines the first valid socket path from the list of candidates.
 */
void SocketManager::determineSocketPath() {
    for (const QString &path : socketPaths) {
        qDebug() << "Checking socket path:" << path;
        if (QFile::exists(path)) {
            QLocalSocket socket;
            socket.connectToServer(path);
            if (socket.waitForConnected(500)) {
                activeSocketPath = path;
                qDebug() << "Using active socket path:"
                         << activeSocketPath;
                return;
            } else {
                qDebug() << "Socket path exists but cannot be connected to:"
                         << path;
            }
        } else {
            qDebug() << "Socket path does not exist:" << path;
        }
    }
    qDebug() << "No valid socket path found among candidates.";
}