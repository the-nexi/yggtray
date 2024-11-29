/**
 * @file SocketManager.h
 * @brief Header file for the SocketManager class.
 * 
 * Handles communication with a UNIX domain socket for sending requests
 * and receiving responses.
 */

#ifndef SOCKETMANAGER_H
#define SOCKETMANAGER_H

#include <QString>
#include <QJsonObject>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QDebug>

/**
 * @class SocketManager
 * @brief Manages communication with a UNIX domain socket.
 * 
 * This class provides methods for sending requests to and receiving
 * responses from a UNIX domain socket, as well as retrieving specific
 * information such as the Yggdrasil IP address.
 */
class SocketManager {
public:
    /**
     * @brief Constructs a SocketManager for the specified socket path.
     * @param socketPath The path to the UNIX domain socket.
     */
    explicit SocketManager(const QString &socketPath) : socketPath(socketPath) {}

    /**
     * @brief Sends a request to the socket and receives the response.
     * @param request A QJsonObject containing the request data.
     * @return A QJsonObject containing the response, or an empty object if an error occurred.
     * 
     * This method handles connecting to the socket, sending the request,
     * and reading the response. It logs any errors encountered during the process.
     */
    QJsonObject sendRequest(const QJsonObject &request) {
        QLocalSocket socket;
        socket.connectToServer(socketPath);

        if (!socket.waitForConnected(3000)) {
            qDebug() << "Failed to connect to socket at" << socketPath;
            return {};
        }

        QByteArray requestData = QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n";
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
     * @return A QString containing the IP address, or "Unknown" if an error occurred.
     * 
     * This method sends a "getself" request to the socket and parses
     * the response to extract the IP address.
     */
    QString getYggdrasilIP() {
        QJsonObject response = sendRequest({{"request", "getself"}});
        if (response.contains("response") && response["response"].isObject()) {
            return response["response"].toObject()["address"].toString();
        }
        return "Unknown";
    }

private:
    QString socketPath; ///< The path to the UNIX domain socket.
};

#endif // SOCKETMANAGER_H

