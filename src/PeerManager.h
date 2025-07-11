#ifndef PEERMANAGER_H
#define PEERMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QElapsedTimer>
#include <QList>
#include <QThreadPool>
#include <QRunnable>
#include <QAtomicInt>
#include <QMutex>

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

// Forward declaration
class PeerManager;

/**
 * @class PeerTestRunnable
 * @brief Runnable task for testing a single peer's latency using QThreadPool.
 *
 * @details Encapsulates the logic for pinging a single peer and reporting results.
 *          Designed to be run concurrently by a QThreadPool.
 *          Includes cancellation support via a shared QAtomicInt.
 */
class PeerTestRunnable : public QObject, public QRunnable {
    Q_OBJECT

public:
    // Constants for ping test configuration
    static constexpr int PING_COUNT = 3;            // Number of pings to send
    static constexpr int CHECK_INTERVAL_MS = 100;   // Interval to check ping status
    static constexpr int PING_TIMEOUT_MS = 5000;    // Total timeout for ping operation

    /**
     * @brief Constructor for PeerTestRunnable
     * @param peer The peer data to test.
     * @param cancelFlag Pointer to the shared cancellation flag.
     * @param parent Optional QObject parent.
     */
    explicit PeerTestRunnable(PeerData peer, QAtomicInt* cancelFlag, QObject *parent = nullptr);

    /**
     * @brief The main execution method for the runnable task.
     * @details Overrides QRunnable::run(). Performs the ping test.
     */
    void run() override;

signals:
    /**
     * @brief Emitted when the peer test is complete.
     * @param peer The PeerData structure with updated latency and validity.
     */
    void peerTested(const PeerData& peer);

private:
    PeerData peerData;
    QAtomicInt* cancelFlagPtr; // Shared cancellation flag
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
     * @brief Sets the proxy to use for peer fetching network requests
     * @param proxy The QNetworkProxy to use
     */
    void setPeerFetchProxy(const QNetworkProxy& proxy);

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

    /**
     * @brief Exports the given list of peers to a CSV file
     * @param fileName The full path to the CSV file to be created/overwritten
     * @param peerList The list of peers to export
     * @return true if the export was successful, false otherwise
     */
    bool exportPeersToCsv(const QString& fileName, const QList<PeerData>& peerList);

signals:
    void peersDiscovered(const QList<PeerData>& peers);
    void peerTested(const PeerData& peer);
    void error(const QString& message);
    // Removed: void requestTestPeer(PeerData peer);

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
    QThreadPool* threadPool;
    QAtomicInt cancelTestsFlag;
    bool debugMode;
};

#endif // PEERMANAGER_H
