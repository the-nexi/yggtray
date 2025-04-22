/**
 * @file PeerManager.cpp
 * @brief Implementation file for the PeerManager class.
 *
 * Contains implementation of methods for managing Yggdrasil peers.
 */

#include "PeerManager.h"
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>
#include <QProcess>
#include <QDebug>
#include <QFile>
#include <QThreadPool>

/**
 * @brief Constructor for PeerTestRunnable
 * @param peer The peer data to test.
 * @param cancelFlag Pointer to the shared cancellation flag.
 * @param parent Optional QObject parent.
 */
PeerTestRunnable::PeerTestRunnable(PeerData peer, QAtomicInt* cancelFlag, QObject *parent)
    : QObject(parent), QRunnable(), peerData(peer), cancelFlagPtr(cancelFlag) {
    setAutoDelete(true); 
}

/**
 * @brief The main execution method for the runnable task.
 * @details Overrides QRunnable::run(). Performs the ping test.
 */
void PeerTestRunnable::run() {
    if (cancelFlagPtr && cancelFlagPtr->loadAcquire()) {
        qDebug() << "[PeerTestRunnable::run] Skipping test for:" << peerData.host << "(cancelled before start)";
        emit peerTested(peerData); 
        return;
    }

    qDebug() << "[PeerTestRunnable::run] Starting test for:" << peerData.host << "on thread" << QThread::currentThreadId();

    QProcess pingProcess; 
    QStringList args;
    QString hostToPing = peerData.host;
    if (hostToPing.contains("://")) {
        hostToPing = hostToPing.split("://").last();
    }
    if (hostToPing.contains("]:")) { // IPv6 with port
        hostToPing = hostToPing.section(']', 0, 0).mid(1); // Get content inside []
    } else if (hostToPing.contains(':')) { // IPv4 with port
        hostToPing = hostToPing.section(':', 0, 0);
    }
    
    args << "-c" << QString::number(PING_COUNT) << hostToPing;

    qDebug() << "[PeerTestRunnable::run] Running ping command - host:" << hostToPing << "args:" << args;
    pingProcess.start("ping", args);

    int timeoutRemaining = PING_TIMEOUT_MS;

    // Loop while waiting for the process to finish, checking for cancellation
    while (!pingProcess.waitForFinished(CHECK_INTERVAL_MS)) {
        if (cancelFlagPtr && cancelFlagPtr->loadAcquire()) {
            qDebug() << "[PeerTestRunnable::run] Ping cancelled for:" << peerData.host;
            if (pingProcess.state() == QProcess::Running) {
                pingProcess.terminate();
                if (!pingProcess.waitForFinished(500)) { 
                    qDebug() << "[PeerTestRunnable::run] Ping terminate failed, killing process for:" << peerData.host;
                    pingProcess.kill();
                    pingProcess.waitForFinished(100); 
                }
            }
            emit peerTested(peerData); 
            return; 
        }

        timeoutRemaining -= CHECK_INTERVAL_MS;
        if (timeoutRemaining <= 0) {
            qDebug() << "[PeerTestRunnable::run] Ping timeout after" << PING_TIMEOUT_MS << "ms for:" << peerData.host;
            if (pingProcess.state() == QProcess::Running) {
                pingProcess.terminate();
                 if (!pingProcess.waitForFinished(500)) {
                    qDebug() << "[PeerTestRunnable::run] Ping terminate failed on timeout, killing process for:" << peerData.host;
                    pingProcess.kill();
                    pingProcess.waitForFinished(100);
                 }
            }
            emit peerTested(peerData); 
            return; 
        }
    }

    if (cancelFlagPtr && cancelFlagPtr->loadAcquire()) {
        qDebug() << "[PeerTestRunnable::run] Test cancelled after ping completion for:" << peerData.host;
        emit peerTested(peerData); 
        return;
    }

    // Process results only if not cancelled and process finished normally
    if (pingProcess.exitStatus() == QProcess::NormalExit && pingProcess.exitCode() == 0) {
        QString output = pingProcess.readAllStandardOutput();
        QRegularExpression rx("min/avg/max(?:/mdev)? = [\\d.]+/([\\d.]+)/[\\d.]+"); 
        auto match = rx.match(output);
        qDebug() << "[PeerTestRunnable::run] Ping output for:" << peerData.host << "-" << output.trimmed();
        if (match.hasMatch()) {
            bool ok;
            double latency = match.captured(1).toDouble(&ok);
            if (ok) {
                peerData.latency = static_cast<int>(latency + 0.5); 
                peerData.isValid = true;
                qDebug() << "[PeerTestRunnable::run] Latency for:" << peerData.host << "-" << peerData.latency << "ms";
            } else {
                 qDebug() << "[PeerTestRunnable::run] Failed to parse latency double for:" << peerData.host;
                 peerData.isValid = false; 
            }
        } else {
            qDebug() << "[PeerTestRunnable::run] No latency match in ping output for:" << peerData.host;
            peerData.isValid = false; 
        }
    } else {
         qDebug() << "[PeerTestRunnable::run] Ping process failed or exited abnormally for:" << peerData.host 
                  << "ExitCode:" << pingProcess.exitCode() << "ExitStatus:" << pingProcess.exitStatus();
         peerData.isValid = false; 
    }

    qDebug() << "[PeerTestRunnable::run] Emitting peerTested signal - host:" << peerData.host << "isValid:" << peerData.isValid << "latency:" << peerData.latency;
    emit peerTested(peerData);
}


/**
 * @brief Exports the given list of peers to a CSV file
 * @param fileName The full path to the CSV file to be created/overwritten
 * @param peerList The list of peers to export
 * @return true if the export was successful, false otherwise
 */
bool PeerManager::exportPeersToCsv(const QString& fileName, const QList<PeerData>& peerList) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[PeerManager::exportPeersToCsv] Could not open file for writing:" << fileName << file.errorString();
        // Note: Cannot emit 'error' signal directly here as this function might be called from different contexts.
        // Consider returning an error code or string instead if more detailed error handling is needed upstream.
        return false;
    }

    QTextStream out(&file);
    out << "\"Host\",\"Latency (ms)\",\"Valid\"\n";

    for (const PeerData& peer : peerList) {
        QString latencyStr;
        // Determine latency string based solely on PeerData
        if (peer.latency < -1) { // Assuming latency < -1 might indicate some other failure state, treat as Failed
             latencyStr = tr("Failed");
        } else if (peer.latency == -1) { // latency == -1 indicates not tested
             latencyStr = tr("Not Tested");
        } else { // latency >= 0 is a valid measurement
            latencyStr = QString::number(peer.latency);
        }

        QString validityStr = ""; // Default to empty string
        // Determine validity string based solely on PeerData, only if tested
        if (peer.latency != -1) { // Only show validity if the peer was actually tested (latency is not -1)
             validityStr = peer.isValid ? tr("Valid") : tr("Invalid");
        }

        out << "\"" << peer.host << "\","
            << "\"" << latencyStr << "\","
            << "\"" << validityStr << "\"\n";
    }

    file.close();
    qDebug() << "[PeerManager::exportPeersToCsv] Successfully exported" << peerList.count() << "peers to" << fileName;
    return true;
}

/**
 * @brief Constructor for PeerManager
 * @param debugMode Whether to enable debug output
 * @param parent Parent QObject
 */
PeerManager::PeerManager(bool debugMode, QObject *parent) 
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , threadPool(new QThreadPool(this)) 
    , cancelTestsFlag(0)               
    , debugMode(debugMode) {
    
    qRegisterMetaType<PeerData>("PeerData");
    qRegisterMetaType<QList<PeerData>>("QList<PeerData>");
    
    connect(networkManager, &QNetworkAccessManager::finished,
            this, &PeerManager::handleNetworkResponse);
            
    threadPool->setMaxThreadCount(5); 
    qDebug() << "[PeerManager] Thread pool initialized with max" << threadPool->maxThreadCount() << "threads.";
}

/**
 * @brief Destructor for PeerManager
 */
PeerManager::~PeerManager() {
    qDebug() << "[PeerManager::~PeerManager] Cleaning up...";
    cancelTests(); 
    threadPool->clear(); 
    qDebug() << "[PeerManager::~PeerManager] Waiting for active tests to finish...";
    bool allFinished = threadPool->waitForDone(-1); 
    qDebug() << "[PeerManager::~PeerManager] All tests finished:" << allFinished;
}

/**
 * @brief Fetches peer list from public peers repository
 */
void PeerManager::fetchPeers() {
    QNetworkRequest request(QUrl("https://publicpeers.neilalexander.dev/"));
    networkManager->get(request);
}

/**
 * @brief Extracts hostname from a peer URI
 * @param peerUri The peer URI to parse
 * @return The hostname if found, empty string otherwise
 */
QString PeerManager::getHostname(const QString& peerUri) const {
    QRegularExpression re("(?:tls|tcp|quic)://\\[?([a-zA-Z0-9:.\\-]+)\\]?:");
    auto match = re.match(peerUri);
    return match.hasMatch() ? match.captured(1) : QString();
}

/**
 * @brief Tests peer connection quality asynchronously
 * @param peer The peer to test
 */
void PeerManager::testPeer(PeerData peer) {
    PeerTestRunnable* task = new PeerTestRunnable(peer, &cancelTestsFlag);

    connect(task, &PeerTestRunnable::peerTested, 
            this, &PeerManager::handlePeerTested, 
            Qt::QueuedConnection);

    qDebug() << "[PeerManager::testPeer] Submitting test task for:" << peer.host;
    threadPool->start(task); 
}

/**
 * @brief Resets the cancellation flag to allow new tests to run
 */
void PeerManager::resetCancellation() {
    qDebug() << "[PeerManager::resetCancellation] Resetting cancellation flag.";
    cancelTestsFlag.storeRelease(0); 
}

/**
 * @brief Cancels all ongoing peer tests
 */
void PeerManager::cancelTests() {
    qDebug() << "[PeerManager::cancelTests] Requesting cancellation of all active tests.";
    cancelTestsFlag.storeRelease(1); 
    threadPool->clear(); 
    qDebug() << "[PeerManager::cancelTests] Cancellation flag set and thread pool queue cleared.";
}

/**
 * @brief Extracts a resource file to a specified location
 * @param resourcePath Path to the resource file in Qt's resource system
 * @param outputPath Path where the resource should be extracted
 * @return true if extraction was successful, false otherwise
 * @details If the resource has .sh extension, the output file will be made executable
 */
bool PeerManager::extractResource(const QString& resourcePath, const QString& outputPath) {
    QFile resourceFile(resourcePath);
    if (!resourceFile.open(QIODevice::ReadOnly)) {
        qDebug() << "[PeerManager::extractResource] Failed to open resource:" << resourcePath;
        return false;
    }

    QFile outputFile(outputPath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        qDebug() << "[PeerManager::extractResource] Failed to create output file:" << outputPath;
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

/**
 * @brief Updates Yggdrasil configuration with selected peers
 * @param selectedPeers List of peers to include in configuration
 * @return true if configuration was successfully updated
 */
bool PeerManager::updateConfig(const QList<PeerData>& selectedPeers) {
    qDebug() << "[PeerManager::updateConfig] Starting update with" << selectedPeers.count() << "peers";
    
    // Count valid peers for logging
    int totalValidPeers = std::count_if(selectedPeers.begin(), selectedPeers.end(), 
                                       [](const PeerData& p) { return p.isValid; });
    qDebug() << "[PeerManager::updateConfig] Valid peers in selection:" << totalValidPeers;
    
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
        qDebug() << "[PeerManager::updateConfig] Failed to extract update script";
        return false;
    }
    
    if (!extractResource(":/polkit/org.yggtray.updatepeers.policy", policyPath)) {
        // Clean up script if policy extraction fails
        QFile::remove(scriptPath);
        qDebug() << "[PeerManager::updateConfig] Failed to extract policy file";
        return false;
    }
    
    // Create temporary file with peer list
    QTemporaryFile peersFile;
    if (!peersFile.open()) {
        qDebug() << "[PeerManager::updateConfig] Failed to create temporary peers file:" << peersFile.errorString();
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
        qDebug() << "[PeerManager::updateConfig] Warning: No valid peers found, using all peers as fallback";
        stream.seek(0); // Reset the stream position
        
        // Use all peers instead, sorted by latency if available
        for (const auto& peer : sortedPeers) {
            stream << peer.host << "\n";
        }
        
        qDebug() << "[PeerManager::updateConfig] Writing" << sortedPeers.count() << "peers to config (up to" << MAX_PEERS << "will be used)";
    } else {
        qDebug() << "[PeerManager::updateConfig] Writing" << validPeerCount << "valid peers to config (up to" << MAX_PEERS << "will be used)";
    }
    
    stream.flush();
    
    // Debug: Read back file content to verify it's not empty
    peersFile.seek(0);
    QString fileContent = QString::fromUtf8(peersFile.readAll());
    qDebug() << "[PeerManager::updateConfig] Verifying peers file:" << (fileContent.isEmpty() ? "EMPTY!" : "Contains data");
    peersFile.seek(0);  // Reset position for the script to read
    
    // Execute update script with elevated privileges
    QProcess process;
    QStringList args;
    if (debugMode) {
        args << "sh" << scriptPath << "--verbose" << peersFile.fileName();
    } else {
        args << "sh" << scriptPath << peersFile.fileName();
    }
    
    qDebug() << "[PeerManager::updateConfig] Executing update script - command: pkexec" << args;
    process.start("pkexec", args);
    
    if (!process.waitForFinished(SCRIPT_TIMEOUT_MS)) {
        QString errorMsg = "Update script timed out";
        qDebug() << "[PeerManager::updateConfig] Error:" << errorMsg;
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
            qDebug() << "[PeerManager::updateConfig] Script exited with code 1 but reported success, treating as successful";
            
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
        
        qDebug() << "[PeerManager::updateConfig] Error:" << errorMsg;
        QFile::remove(scriptPath);
        QFile::remove(policyPath);
        emit error(errorMsg);
        return false;
    }
    
    // Log the success output
    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        qDebug() << "[PeerManager::updateConfig] Script output:" << output;
    }
    
    // Clean up temporary files
    QFile::remove(scriptPath);
    QFile::remove(policyPath);
    return true;
}

/**
 * @brief Handles the response from public peers repository
 * @param reply Network reply containing peer list HTML
 */
void PeerManager::handleNetworkResponse(QNetworkReply* reply) {
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

/**
 * @brief Handles the completion of a peer test
 * @param peer The tested peer with updated latency and validity
 */
void PeerManager::handlePeerTested(const PeerData& peer) {
    qDebug() << "[PeerManager::handlePeerTested] Received result for:" << peer.host << "on thread" << QThread::currentThreadId();
    emit peerTested(peer); 
}
