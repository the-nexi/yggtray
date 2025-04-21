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

/**
 * @brief Tests peer connection quality asynchronously
 * @param peer The peer to test
 */
void PeerTester::testPeer(PeerData peer) {
    if (cancelRequested.loadAcquire()) {
        qDebug() << "[PeerTester::testPeer] Skipping test for:" << peer.host << "(cancelled)";
        return;
    }

    qDebug() << "[PeerTester::testPeer] Starting test for:" << peer.host;

    QProcess* pingProcess = new QProcess(this);
    QStringList args;
    QString hostToPing = peer.host.split("://").last().split(":").first();
    args << "-c" << QString::number(PING_COUNT) << hostToPing;
    
    {
        QMutexLocker locker(&processesMutex);
        activeProcesses.append(pingProcess);
    }
    
    connect(pingProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [this, pingProcess]() {
                QMutexLocker locker(&processesMutex);
                activeProcesses.removeAll(pingProcess);
                pingProcess->deleteLater();
            });
    
    qDebug() << "[PeerTester::testPeer] Running ping command - host:" << hostToPing << "args:" << args;
    pingProcess->start("ping", args);
    
    int timeoutRemaining = PING_TIMEOUT_MS;
    
    while (!pingProcess->waitForFinished(CHECK_INTERVAL_MS)) {
        if (cancelRequested.loadAcquire()) {
            qDebug() << "[PeerTester::testPeer] Ping cancelled for:" << peer.host;
            // Let the process finish on its own through the connected signal
            pingProcess->terminate(); // Use terminate instead of kill
            // Don't wait here - let the finished signal handle cleanup
            return;
        }
        
        timeoutRemaining -= CHECK_INTERVAL_MS;
        if (timeoutRemaining <= 0) {
            qDebug() << "[PeerTester::testPeer] Ping timeout after" << PING_TIMEOUT_MS << "ms for:" << peer.host;
            pingProcess->terminate();
            break;
        }
    }
    
    if (cancelRequested.loadAcquire()) {
        qDebug() << "[PeerTester::testPeer] Test cancelled after ping completion for:" << peer.host;
        return;
    }
    
    // Process results only if not cancelled
    QString output = pingProcess->readAllStandardOutput();
    QRegularExpression rx("min/avg/max/mdev = \\d+\\.\\d+/([\\d.]+)/\\d+\\.\\d+/\\d+\\.\\d+");
    auto match = rx.match(output);
    qDebug() << "[PeerTester::testPeer] Ping output for:" << peer.host << "-" << output;
    if (match.hasMatch()) {
        peer.latency = match.captured(1).toDouble();
        peer.isValid = true;
        qDebug() << "[PeerTester::testPeer] Latency for:" << peer.host << "-" << peer.latency << "ms";
    } else {
        qDebug() << "[PeerTester::testPeer] No latency match in ping output for:" << peer.host;
    }

    if (!cancelRequested.loadAcquire()) {
        qDebug() << "[PeerTester::testPeer] Emitting peerTested signal - host:" << peer.host << "isValid:" << peer.isValid;
        emit peerTested(peer); // Corrected indentation and removed extra closing brace
    }
}

/**
 * @brief Requests cancellation of all active peer tests
 */
void PeerTester::requestCancel() {
    cancelRequested.storeRelease(1);
    
    // Instead of directly killing processes, just mark them for termination
    // Each process will check this flag and terminate itself in its own thread
    qDebug() << "[PeerTester::requestCancel] Marking" << activeProcesses.size() << "active ping processes for termination";
}

/**
 * @brief Resets the cancellation flag to allow new tests to run
 */
void PeerTester::resetCancellation() {
    cancelRequested.storeRelease(0);
    qDebug() << "[PeerTester::resetCancellation] Cancellation flag reset";
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

/**
 * @brief Destructor for PeerManager
 */
PeerManager::~PeerManager() {
    // Clean up thread on destruction
    cancelTests();
    workerThread->quit();
    workerThread->wait();
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
    emit requestTestPeer(peer);
}

/**
 * @brief Resets the cancellation flag to allow new tests to run
 */
void PeerManager::resetCancellation() {
    if (peerTester) {
        qDebug() << "[PeerManager::resetCancellation] Requesting cancellation flag reset";
        QMetaObject::invokeMethod(peerTester, "resetCancellation", Qt::QueuedConnection);
    }
}

/**
 * @brief Cancels all ongoing peer tests
 */
void PeerManager::cancelTests() {
    if (peerTester) {
        qDebug() << "[PeerManager::cancelTests] Requesting cancellation of all active tests";
        // Use a direct connection to ensure immediate execution
        QMetaObject::invokeMethod(peerTester, "requestCancel", Qt::DirectConnection);
        qDebug() << "[PeerManager::cancelTests] Cancellation request sent to PeerTester";
    } else {
        qDebug() << "[PeerManager::cancelTests] Error: No PeerTester instance available";
    }
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
    emit peerTested(peer);
}
