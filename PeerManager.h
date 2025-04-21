#ifndef PEERMANAGER_H
#define PEERMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QElapsedTimer>
#include <QList>
#include <QThread>
#include <QMutex>
#include <QAtomicInt>

/**
 * @struct PeerData
 * @brief Holds information about a yggdrasil peer
 */
struct PeerData {
    QString host;
    int latency = -1;      // in milliseconds
    bool isValid = false;

    // Define equality operator based on host
    bool operator==(const PeerData& other) const {
        return host == other.host;
    }
};

// Register PeerData for use with queued connections
Q_DECLARE_METATYPE(PeerData)
Q_DECLARE_METATYPE(QList<PeerData>)

/**
 * @class PeerTester
 * @brief Worker class that tests peers in a separate thread
 * 
 * @details Handles network latency testing for Yggdrasil peers in a dedicated thread.
 *          Uses ping command to measure latency and determine peer validity.
 *          Supports cancellation and timeout of ping operations.
 */
class PeerTester : public QObject {
    Q_OBJECT

public:
    // Constants for ping test configuration
    static constexpr int PING_COUNT = 3;            // Number of pings to send
    static constexpr int CHECK_INTERVAL_MS = 100;   // Interval to check ping status
    static constexpr int PING_TIMEOUT_MS = 5000;    // Total timeout for ping operation

public:
    explicit PeerTester(QObject *parent = nullptr) 
        : QObject(parent), cancelRequested(0) {}

public slots:
    void testPeer(PeerData peer);
    void requestCancel();
    void resetCancellation();

signals:
    void peerTested(const PeerData& peer);

private:
    QAtomicInt cancelRequested;
    QList<QProcess*> activeProcesses;
    QMutex processesMutex;
};

/**
 * @class PeerManager
 * @brief Manages Yggdrasil peer discovery, testing, and configuration
 * 
 * @details This class provides functionality for:
 *          - Discovering peers from public repositories
 *          - Testing peer connection quality
 *          - Updating Yggdrasil configuration with selected peers
 *          - Managing peer testing in a separate thread
 *          
 * Thread safety:
 * - Network operations run in the main thread
 * - Peer testing runs in a dedicated worker thread
 * - Resource cleanup is handled automatically
 */
class PeerManager : public QObject {
    Q_OBJECT

public:
    // Constants for configuration
    static constexpr int SCRIPT_TIMEOUT_MS = 30000;    // Timeout for update script execution
    static constexpr int MAX_PEERS = 15;               // Maximum number of peers to use in config

public:
    explicit PeerManager(bool debugMode = false, QObject *parent = nullptr);
    ~PeerManager();

    /**
     * @brief Fetches peer list from public peers repository
     */
    void fetchPeers();

    /**
     * @brief Extracts hostname from a peer URI
     * @param peerUri The peer URI to parse
     * @return The hostname if found, empty string otherwise
     */
    QString getHostname(const QString& peerUri) const;

    /**
     * @brief Tests peer connection quality asynchronously
     * @param peer The peer to test
     */
    void testPeer(PeerData peer);
    
    /**
     * @brief Resets the cancellation flag to allow new tests to run
     */
    void resetCancellation();
    
    /**
     * @brief Cancels all ongoing peer tests
     */
    void cancelTests();

    /**
     * @brief Extracts a resource file to a specified location
     * @param resourcePath Path to the resource file in Qt's resource system
     * @param outputPath Path where the resource should be extracted
     * @return true if extraction was successful, false otherwise
     * @details If the resource has .sh extension, the output file will be made executable
     */
    bool extractResource(const QString& resourcePath, const QString& outputPath);

    /**
     * @brief Updates Yggdrasil configuration with selected peers
     * @param selectedPeers List of peers to include in configuration
     * @return true if configuration was successfully updated
     * @details
     * - Sorts peers by latency (valid peers first, then lowest latency)
     * - Extracts update script and policy file to temporary location
     * - Creates temporary file with peer list
     * - Executes update script with elevated privileges via polkit
     * - Handles special case where exit code 1 with success message is valid
     * - Cleans up temporary files regardless of outcome
     */
    bool updateConfig(const QList<PeerData>& selectedPeers);

signals:
    void peersDiscovered(const QList<PeerData>& peers);
    void peerTested(const PeerData& peer);
    void error(const QString& message);
    void requestTestPeer(PeerData peer);

private slots:
    /**
     * @brief Handles the response from public peers repository
     * @param reply Network reply containing peer list HTML
     * @details Parses HTML to extract peer URIs and emits peersDiscovered signal
     */
    void handleNetworkResponse(QNetworkReply* reply);
    
    /**
     * @brief Handles the completion of a peer test
     * @param peer The tested peer with updated latency and validity
     */
    void handlePeerTested(const PeerData& peer);

private:
    QNetworkAccessManager* networkManager;
    QThread* workerThread;
    PeerTester* peerTester;
    bool debugMode;
};

#endif // PEERMANAGER_H
