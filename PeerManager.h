#ifndef PEERMANAGER_H
#define PEERMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTextStream>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QMetaType>
#include <QAtomicInt>
#include <QDebug>

/**
 * @struct PeerData
 * @brief Holds information about a yggdrasil peer
 */
struct PeerData {
    QString host;
    int latency = -1;      // in milliseconds
    bool isValid = false;
};

// Register PeerData for use with queued connections
Q_DECLARE_METATYPE(PeerData)
Q_DECLARE_METATYPE(QList<PeerData>)

/**
 * @class PeerTester
 * @brief Worker class that tests peers in a separate thread
 */
class PeerTester : public QObject {
    Q_OBJECT

public:
    explicit PeerTester(QObject *parent = nullptr) 
        : QObject(parent), cancelRequested(0) {}

public slots:
    void testPeer(PeerData peer) {
        if (cancelRequested.loadAcquire()) {
            qDebug() << "Skipping test for" << peer.host << "due to cancellation";
            return;
        }

        qDebug() << "Testing peer:" << peer.host;

        // Create process on heap instead of stack
        QProcess* pingProcess = new QProcess(this);
        QStringList args;
        QString hostToPing = peer.host.split("://").last().split(":").first();
        args << "-c" << "3" << hostToPing;
        
        // Add to active processes list with heap-allocated process
        {
            QMutexLocker locker(&processesMutex);
            activeProcesses.append(pingProcess);
        }
        
        // Connect to the process to handle its lifecycle within this thread
        connect(pingProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, pingProcess]() {
                    QMutexLocker locker(&processesMutex);
                    activeProcesses.removeAll(pingProcess);
                    pingProcess->deleteLater();
                });
        
        qDebug() << "Running ping for:" << hostToPing << "with args:" << args;
        pingProcess->start("ping", args);
        
        const int checkIntervalMs = 100;
        int timeoutRemaining = 5000;
        
        while (!pingProcess->waitForFinished(checkIntervalMs)) {
            if (cancelRequested.loadAcquire()) {
                qDebug() << "Ping cancelled for:" << peer.host;
                // Let the process finish on its own through the connected signal
                pingProcess->terminate(); // Use terminate instead of kill
                // Don't wait here - let the finished signal handle cleanup
                return;
            }
            
            timeoutRemaining -= checkIntervalMs;
            if (timeoutRemaining <= 0) {
                qDebug() << "Ping timeout for:" << peer.host;
                pingProcess->terminate();
                break;
            }
        }
        
        if (cancelRequested.loadAcquire()) {
            qDebug() << "Test cancelled after ping completed for:" << peer.host;
            return;
        }
        
        // Process results only if not cancelled
        QString output = pingProcess->readAllStandardOutput();
        QRegularExpression rx("min/avg/max/mdev = \\d+\\.\\d+/([\\d.]+)/\\d+\\.\\d+/\\d+\\.\\d+");
        auto match = rx.match(output);
        qDebug() << "Ping output for" << peer.host << ":" << output;
        if (match.hasMatch()) {
            peer.latency = match.captured(1).toDouble();
            peer.isValid = true;
            qDebug() << "Latency for" << peer.host << ":" << peer.latency << "ms";
        } else {
            qDebug() << "No latency match for:" << peer.host;
        }

        if (!cancelRequested.loadAcquire()) {
            qDebug() << "Emitting peerTested signal for:" << peer.host << "isValid:" << peer.isValid;
            emit peerTested(peer);
        }
    }
    
    void requestCancel() {
        cancelRequested.storeRelease(1);
        
        // Instead of directly killing processes, just mark them for termination
        // Each process will check this flag and terminate itself in its own thread
        qDebug() << "=== CANCELLATION REQUESTED: Marking" << activeProcesses.size() << "active ping processes for termination ===";
    }
    
    void resetCancellation() {
        cancelRequested.storeRelease(0);
        qDebug() << "=== CANCELLATION FLAG RESET ===";
    }

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
 */
class PeerManager : public QObject {
    Q_OBJECT

public:
    explicit PeerManager(bool debugMode = false, QObject *parent = nullptr) 
        : QObject(parent)
        , networkManager(new QNetworkAccessManager(this))
        , debugMode(debugMode) {
        
        // Register PeerData type for queued connections between threads
        qRegisterMetaType<PeerData>("PeerData");
        qRegisterMetaType<QList<PeerData>>("QList<PeerData>");
        
        connect(networkManager, &QNetworkAccessManager::finished,
                this, &PeerManager::handleNetworkResponse);
                
        // Create and start the worker thread
        workerThread = new QThread(this);
        peerTester = new PeerTester();
        peerTester->moveToThread(workerThread);
        
        // Connect signals and slots for thread communication
        connect(this, &PeerManager::requestTestPeer, 
                peerTester, &PeerTester::testPeer, 
                Qt::QueuedConnection);
        connect(peerTester, &PeerTester::peerTested,
                this, &PeerManager::handlePeerTested,
                Qt::QueuedConnection);
        connect(workerThread, &QThread::finished, 
                peerTester, &QObject::deleteLater);
                
        workerThread->start();
    }
    
    ~PeerManager() {
        // Clean up thread on destruction
        cancelTests();
        workerThread->quit();
        workerThread->wait();
    }

    /**
     * @brief Fetches peer list from public peers repository
     */
    void fetchPeers() {
        QNetworkRequest request(QUrl("https://publicpeers.neilalexander.dev/"));
        networkManager->get(request);
    }

    /**
     * @brief Extracts hostname from a peer URI
     * @param peerUri The peer URI to parse
     * @return The hostname if found, empty string otherwise
     */
    QString getHostname(const QString& peerUri) const {
        QRegularExpression re("(?:tls|tcp|quic)://\\[?([a-zA-Z0-9:.\\-]+)\\]?:");
        auto match = re.match(peerUri);
        return match.hasMatch() ? match.captured(1) : QString();
    }

    /**
     * @brief Tests peer connection quality asynchronously
     * @param peer The peer to test
     */
    void testPeer(PeerData peer) {
        emit requestTestPeer(peer);
    }
    
    /**
     * @brief Resets the cancellation flag to allow new tests to run
     */
    void resetCancellation() {
        if (peerTester) {
            qDebug() << "=== PeerManager: Resetting cancellation flag ===";
            QMetaObject::invokeMethod(peerTester, "resetCancellation", Qt::QueuedConnection);
        }
    }
    
    /**
     * @brief Cancels all ongoing peer tests
     */
    void cancelTests() {
        if (peerTester) {
            qDebug() << "=== PeerManager: Requesting cancellation of all tests ===";
            // Use a direct connection to ensure immediate execution
            QMetaObject::invokeMethod(peerTester, "requestCancel", Qt::DirectConnection);
            qDebug() << "=== PeerManager: Cancellation request sent ===";
        } else {
            qDebug() << "=== PeerManager: No peerTester available to cancel tests ===";
        }
    }

    /**
     * @brief Updates yggdrasil config with selected peers
     * @param selectedPeers List of peers to include in config
     * @return true if config was updated successfully
     */
    bool extractResource(const QString& resourcePath, const QString& outputPath) {
        QFile resourceFile(resourcePath);
        if (!resourceFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Failed to open resource:" << resourcePath;
            return false;
        }

        QFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Failed to create file:" << outputPath;
            return false;
        }

        outputFile.write(resourceFile.readAll());
        outputFile.close();

        // Make executable if it's the script
        if (resourcePath.endsWith(".sh")) {
            QFile::setPermissions(outputPath, 
                QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                QFile::ReadGroup | QFile::ExeGroup |
                QFile::ReadOther | QFile::ExeOther);
        }

        return true;
    }

    bool updateConfig(const QList<PeerData>& selectedPeers) {
        qDebug() << "Attempting to update config with" << selectedPeers.count() << "peers";
        
        // Count valid peers for logging
        int totalValidPeers = std::count_if(selectedPeers.begin(), selectedPeers.end(), 
                                           [](const PeerData& p) { return p.isValid; });
        qDebug() << "Total valid peers in selection:" << totalValidPeers;
        
        // Sort peers by latency (lowest first)
        QList<PeerData> sortedPeers = selectedPeers;
        std::sort(sortedPeers.begin(), sortedPeers.end(), 
            [](const PeerData& a, const PeerData& b) {
                if (a.isValid && b.isValid) {
                    return a.latency < b.latency;
                }
                return a.isValid > b.isValid;
            });
        
        // Extract update script to /tmp
        QString scriptPath = "/tmp/yggtray-update-peers.sh";
        QString policyPath = "/tmp/org.yggtray.updatepeers.policy";
        
        if (!extractResource(":/scripts/update-peers.sh", scriptPath)) {
            qDebug() << "Failed to extract update script";
            return false;
        }
        
        if (!extractResource(":/polkit/org.yggtray.updatepeers.policy", policyPath)) {
            // Clean up script if policy extraction fails
            QFile::remove(scriptPath);
            qDebug() << "Failed to extract policy file";
            return false;
        }
        
        // Create temporary file with peer list
        QTemporaryFile peersFile;
        if (!peersFile.open()) {
            qDebug() << "Failed to create temporary file:" << peersFile.errorString();
            return false;
        }
        
        // Write peers to temporary file
        QTextStream stream(&peersFile);
        int validPeerCount = 0;
        
        // First try to write only valid peers
        for (const auto& peer : sortedPeers) {
            if (peer.isValid) {
                stream << peer.host << "\n";
                validPeerCount++;
            }
        }
        
        // If no valid peers, use all peers as a fallback
        if (validPeerCount == 0) {
            qDebug() << "WARNING: No valid peers found. Using all peers as fallback.";
            stream.seek(0); // Reset the stream position
            
            // Use all peers instead, sorted by latency if available
            for (const auto& peer : sortedPeers) {
                stream << peer.host << "\n";
            }
            
            qDebug() << "Writing all" << sortedPeers.count() << "peers to config file (script will use up to 15)";
        } else {
            qDebug() << "Writing" << validPeerCount << "valid peers to config file (script will use up to 15)";
        }
        
        stream.flush();
        
        // Debug: Read back file content to verify it's not empty
        peersFile.seek(0);
        QString fileContent = QString::fromUtf8(peersFile.readAll());
        qDebug() << "Peers file content:" << (fileContent.isEmpty() ? "EMPTY!" : "Contains data");
        peersFile.seek(0);  // Reset position for the script to read
        
        // Execute update script with elevated privileges
        QProcess process;
        QStringList args;
        if (debugMode) {
            args << "sh" << scriptPath << "--verbose" << peersFile.fileName();
        } else {
            args << "sh" << scriptPath << peersFile.fileName();
        }
        
        qDebug() << "Executing:" << "pkexec" << args;
        process.start("pkexec", args);
        
        if (!process.waitForFinished(30000)) { // 30 second timeout
            QString errorMsg = "Update script timed out";
            qDebug() << errorMsg;
            QFile::remove(scriptPath);
            QFile::remove(policyPath);
            emit error(errorMsg);
            return false;
        }
        
        if (process.exitCode() != 0) {
            QString stdErr = QString::fromUtf8(process.readAllStandardError());
            QString stdOut = QString::fromUtf8(process.readAllStandardOutput());
            
            // Special case: If the output contains "updated successfully" but exit code is non-zero,
            // consider it a success and ignore the exit code
            if ((stdOut.contains("updated successfully") || stdErr.contains("updated successfully")) && 
                process.exitCode() == 1) {
                qDebug() << "Script exited with code 1 but reported success in output, treating as success";
                
                // Clean up temporary files
                QFile::remove(scriptPath);
                QFile::remove(policyPath);
                return true;
            }
            
            QString errorMsg = "Update script failed with exit code " + QString::number(process.exitCode());
            
            if (!stdErr.isEmpty()) {
                errorMsg += ": " + stdErr.trimmed();
            } else if (!stdOut.isEmpty()) {
                errorMsg += ": " + stdOut.trimmed();
            }
            
            qDebug() << errorMsg;
            QFile::remove(scriptPath);
            QFile::remove(policyPath);
            emit error(errorMsg);
            return false;
        }
        
        // Log the success output
        QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            qDebug() << "Script output:" << output;
        }
        
        // Clean up temporary files
        QFile::remove(scriptPath);
        QFile::remove(policyPath);
        return true;
    }

signals:
    void peersDiscovered(const QList<PeerData>& peers);
    void peerTested(const PeerData& peer);
    void error(const QString& message);
    void requestTestPeer(PeerData peer);

private slots:
    void handleNetworkResponse(QNetworkReply* reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QString html = QString::fromUtf8(reply->readAll());
            QList<PeerData> peers;
            
            // Find all <td> elements containing peer URIs
            QRegularExpression tdRe("<td[^>]*>([^<]+)</td>");
            auto it = tdRe.globalMatch(html);
            
            while (it.hasNext()) {
                auto match = it.next();
                QString peerUri = match.captured(1).trimmed();
                QString hostname = getHostname(peerUri);
                
                if (!hostname.isEmpty()) {
                    PeerData peer;
                    peer.host = peerUri;
                    peers.append(peer);
                }
            }
            
            emit peersDiscovered(peers);
        } else {
            emit error(tr("Failed to fetch peers: %1").arg(reply->errorString()));
        }
        
        reply->deleteLater();
    }
    
    void handlePeerTested(const PeerData& peer) {
        emit peerTested(peer);
    }

private:
    QNetworkAccessManager* networkManager;
    QThread* workerThread;
    PeerTester* peerTester;
    bool debugMode;
};

#endif // PEERMANAGER_H
