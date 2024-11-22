#ifndef SOCKETMANAGER_H
#define SOCKETMANAGER_H

#include <QString>
#include <QJsonObject>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QDebug>

class SocketManager {
public:
    explicit SocketManager(const QString &socketPath) : socketPath(socketPath) {}

    // Send a request to the socket and receive the response
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

    // Get Yggdrasil IP address
    QString getYggdrasilIP() {
        QJsonObject response = sendRequest({{"request", "getself"}});
        if (response.contains("response") && response["response"].isObject()) {
            return response["response"].toObject()["address"].toString();
        }
        return "Unknown";
    }

private:
    QString socketPath;
};

#endif // SOCKETMANAGER_H

