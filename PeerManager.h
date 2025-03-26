#ifndef PEERMANAGER_H
#define PEERMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTextStream>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QMetaType>
#include <QAtomicInt>

/**
 * @struct PeerData
 * @brief Holds information about a yggdrasil peer
 */
struct PeerData {
    QString host;
    int latency = -1;      // in milliseconds
    double speed = -1.0;   // in Mbps
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
        // Check if cancellation was requested before starting the test
        if (cancelRequested.loadAcquire()) {
            return;
        }

        // Test latency using ping
        QProcess pingProcess;
        QStringList args;
        args << "-c" << "3" << peer.host.split("://").last().split(":").first();
        
        pingProcess.start("ping", args);
        
        // Wait for process to finish but check for cancellation periodically
        const int checkIntervalMs = 100;
        int timeoutRemaining = 5000;
        
        while (!pingProcess.waitForFinished(checkIntervalMs)) {
            // Check if cancellation was requested
            if (cancelRequested.loadAcquire()) {
                pingProcess.kill();
                return;
            }
            
            timeoutRemaining -= checkIntervalMs;
            if (timeoutRemaining <= 0) {
                // Timeout occurred
                pingProcess.kill();
                break;
            }
        }
        
        if (pingProcess.state() == QProcess::NotRunning && !cancelRequested.loadAcquire()) {
            QString output = pingProcess.readAllStandardOutput();
            // Parse average latency from ping output
            QRegularExpression rx("min/avg/max/mdev = \\d+\\.\\d+/([\\d.]+)/\\d+\\.\\d+/\\d+\\.\\d+");
            auto match = rx.match(output);
            if (match.hasMatch()) {
                peer.latency = match.captured(1).toDouble();
                peer.isValid = true;
            }
        }

        // Test connection speed using a simple data transfer
        if (peer.isValid && !cancelRequested.loadAcquire()) {
            testConnectionSpeed(peer);
        }

        // If cancellation wasn't requested, emit completion signal
        if (!cancelRequested.loadAcquire()) {
            emit peerTested(peer);
        }
    }
    
    void requestCancel() {
        cancelRequested.storeRelease(1);
    }

signals:
    void peerTested(const PeerData& peer);

private:
    void testConnectionSpeed(PeerData& peer) {
        // Implement a simple speed test by measuring data transfer time
        QTcpSocket socket;
        socket.connectToHost(peer.host.split("://").last().split(":").first(),
                           peer.host.split(":").last().toInt());
        
        // Wait for connection but check for cancellation periodically
        const int checkIntervalMs = 100;
        int timeoutRemaining = 5000;
        
        while (!socket.waitForConnected(checkIntervalMs)) {
            // Check if cancellation was requested
            if (cancelRequested.loadAcquire()) {
                socket.abort();
                return;
            }
            
            timeoutRemaining -= checkIntervalMs;
            if (timeoutRemaining <= 0 || socket.error() != QAbstractSocket::SocketTimeoutError) {
                // Timeout or error occurred
                socket.abort();
                return;
            }
        }
        
        if (socket.state() == QAbstractSocket::ConnectedState && !cancelRequested.loadAcquire()) {
            QElapsedTimer timer;
            timer.start();
            
            // Send test data and measure transfer time
            QByteArray testData(1024 * 100, 'x'); // 100KB test data
            socket.write(testData);
            
            // Wait for bytes written but check for cancellation periodically
            timeoutRemaining = 5000;
            while (!socket.waitForBytesWritten(checkIntervalMs)) {
                // Check if cancellation was requested
                if (cancelRequested.loadAcquire()) {
                    socket.abort();
                    return;
                }
                
                timeoutRemaining -= checkIntervalMs;
                if (timeoutRemaining <= 0) {
                    // Timeout occurred
                    socket.abort();
                    return;
                }
            }
            
            int elapsed = timer.elapsed();
            if (elapsed > 0) {
                // Calculate speed in Mbps
                peer.speed = (testData.size() * 8.0 / 1000000.0) / (elapsed / 1000.0);
            }
            
            socket.disconnectFromHost();
        }
    }
    
    QAtomicInt cancelRequested;
};

/**
 * @class PeerManager
 * @brief Manages Yggdrasil peer discovery, testing, and configuration
 */
class PeerManager : public QObject {
    Q_OBJECT

public:
    explicit PeerManager(QObject *parent = nullptr) 
        : QObject(parent)
        , networkManager(new QNetworkAccessManager(this))
        , configPath("/etc/yggdrasil.conf") {
        
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
                peerTester, &PeerTester::testPeer);
        connect(peerTester, &PeerTester::peerTested,
                this, &PeerManager::handlePeerTested);
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
     * @brief Cancels all ongoing peer tests
     */
    void cancelTests() {
        if (peerTester) {
            QMetaObject::invokeMethod(peerTester, "requestCancel", Qt::QueuedConnection);
        }
    }

    /**
     * @brief Updates yggdrasil config with selected peers
     * @param selectedPeers List of peers to include in config
     * @return true if config was updated successfully
     */
    bool updateConfig(const QList<PeerData>& selectedPeers) {
        QFile file(configPath);
        if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            return false;
        }

        QString config = file.readAll();
        
        // Find the Peers section in the config
        QRegularExpression peersRe("(\\s*Peers:\\s*\\[)[^\\]]*\\]");
        QString newPeersList = "[\n";
        
        // Sort peers by latency (lowest first)
        QList<PeerData> sortedPeers = selectedPeers;
        std::sort(sortedPeers.begin(), sortedPeers.end(), 
            [](const PeerData& a, const PeerData& b) {
                // If both are valid, compare by latency
                if (a.isValid && b.isValid) {
                    return a.latency < b.latency;
                }
                // Valid peers come before invalid ones
                return a.isValid > b.isValid;
            });
        
        for (const auto& peer : sortedPeers) {
            if (peer.isValid) {
                // Format without quotes as per example
                newPeersList += QString("    %1\n").arg(peer.host);
            }
        }
        
        // Close the bracket
        newPeersList += "  ]";
        
        QString newConfig;
        if (peersRe.match(config).hasMatch()) {
            // Replace existing Peers section
            newConfig = config.replace(peersRe, "\\1" + newPeersList);
        } else {
            // Add new Peers section
            newConfig = config.trimmed();
            if (!newConfig.isEmpty() && !newConfig.endsWith("\n")) {
                newConfig += "\n";
            }
            newConfig += "  Peers: " + newPeersList + "\n";
        }
        
        // Write updated config
        file.seek(0);
        file.write(newConfig.toUtf8());
        file.resize(file.pos());
        
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
    QString configPath;
    QThread* workerThread;
    PeerTester* peerTester;
};

#endif // PEERMANAGER_H
