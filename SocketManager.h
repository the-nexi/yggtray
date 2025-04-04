#ifndef SOCKETMANAGER_H
#define SOCKETMANAGER_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QLocalSocket>

/**
 * @class SocketManager
 * @brief Manages communication with a UNIX domain socket.
 */
class SocketManager {
public:
    /**
     * @brief Constructs a SocketManager with multiple possible socket paths.
     * @param possibleSocketPaths A list of potential UNIX domain socket paths.
     */
    explicit SocketManager(const QStringList &possibleSocketPaths);

    /**
     * @brief Sends a request to the socket and receives the response.
     * @param request A QJsonObject containing the request data.
     * @return A QJsonObject containing the response, or an empty object if an error occurred.
     */
    QJsonObject sendRequest(const QJsonObject &request);

    /**
     * @brief Retrieves the Yggdrasil IP address from the socket.
     *
     * @return A QString containing the IP address, or "Unknown" if an error
     * occurred.
     */
    QString getYggdrasilIP();

private:
    QStringList socketPaths;   ///< List of possible socket paths.
    QString activeSocketPath; ///< The active socket path.

    /**
     * @brief Determines the first valid socket path from the list of
     * candidates.
     */
    void determineSocketPath();
};

#endif // SOCKETMANAGER_H

