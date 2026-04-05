/**
 * @file PeerManager.cpp
 * @brief Implementation file for the PeerManager class.
 *
 * Contains implementation of methods for managing Yggdrasil peers.
 */

#include <memory>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThreadPool>

#include "PeerManager.h"

static const QString SCRIPT_PATH = "/tmp/yggtray-update-peers.sh";
static const QString POLICY_PATH = "/tmp/org.yggtray.updatepeers.policy";

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
        qDebug() << "[PeerTestRunnable::run] Skipping test for:"
                 << peerData.host
                 << "(cancelled before start)";
        emit peerTested(peerData);
        return;
    }

    qDebug() << "[PeerTestRunnable::run] Starting test for:"
             << peerData.host << "on thread" << QThread::currentThreadId();

    QProcess pingProcess;
    QStringList args;
    QString hostToPing = peerData.host;
    if (hostToPing.contains("://")) {
        hostToPing = hostToPing.split("://").last();
    }
    if (hostToPing.contains("]:")) { // IPv6 with port
        // Get content inside []
        hostToPing = hostToPing.section(']', 0, 0).mid(1);
    } else if (hostToPing.contains(':')) { // IPv4 with port
        hostToPing = hostToPing.section(':', 0, 0);
    }

    args << "-c" << QString::number(PING_COUNT) << hostToPing;

    qDebug() << "[PeerTestRunnable::run] Running ping command - host:"
             << hostToPing << "args:" << args;
    pingProcess.start("ping", args);

    int timeoutRemaining = PING_TIMEOUT_MS;

    // Loop while waiting for the process to finish, checking for cancellation
    while (!pingProcess.waitForFinished(CHECK_INTERVAL_MS)) {
        if (cancelFlagPtr && cancelFlagPtr->loadAcquire()) {
            qDebug() << "[PeerTestRunnable::run] Ping cancelled for:" << peerData.host;
            if (pingProcess.state() == QProcess::Running) {
                pingProcess.terminate();
                if (!pingProcess.waitForFinished(500)) {
                    qDebug() << "[PeerTestRunnable::run]"
                             << "Ping terminate failed, killing process for:"
                             << peerData.host;
                    pingProcess.kill();
                    pingProcess.waitForFinished(100);
                }
            }
            emit peerTested(peerData);
            return;
        }

        timeoutRemaining -= CHECK_INTERVAL_MS;
        if (timeoutRemaining <= 0) {
            qDebug() << "[PeerTestRunnable::run]"
                     << "Ping timeout after"
                     << PING_TIMEOUT_MS
                     << "ms for:" << peerData.host;
            if (pingProcess.state() == QProcess::Running) {
                pingProcess.terminate();
                 if (!pingProcess.waitForFinished(500)) {
                    qDebug() << "[PeerTestRunnable::run]"
                             << "Ping terminate failed on timeout, killing process for:"
                             << peerData.host;
                    pingProcess.kill();
                    pingProcess.waitForFinished(100);
                 }
            }
            emit peerTested(peerData);
            return;
        }
    }

    if (cancelFlagPtr && cancelFlagPtr->loadAcquire()) {
        qDebug() << "[PeerTestRunnable::run]"
                 << "Test cancelled after ping completion for:"
                 << peerData.host;
        emit peerTested(peerData);
        return;
    }

    // Process results only if not cancelled and process finished normally
    if ((pingProcess.exitStatus() == QProcess::NormalExit)
        && (pingProcess.exitCode() == 0)) {
        QString output = pingProcess.readAllStandardOutput();
        QRegularExpression rx("min/avg/max(?:/mdev)? = [\\d.]+/([\\d.]+)/[\\d.]+");
        auto match = rx.match(output);
        qDebug() << "[PeerTestRunnable::run] Ping output for:"
                 << peerData.host << "-" << output.trimmed();
        if (match.hasMatch()) {
            bool ok;
            double latency = match.captured(1).toDouble(&ok);
            if (ok) {
                if (latency <= 0.0) {
                    qDebug() << "[PeerTestRunnable::run]"
                             << "Invalid zero or negative latency for:"
                             << peerData.host;
                    peerData.latency = -1;
                    peerData.isValid = false;
                } else {
                    // Round to integer milliseconds with minimum of 1ms
                    peerData.latency
                        = std::max(1, static_cast<int>(latency + 0.5));
                    peerData.isValid = true;
                    qDebug() << "[PeerTestRunnable::run] Latency for:"
                             << peerData.host << "-"
                             << peerData.latency << "ms";
                }
            } else {
                qDebug() << "[PeerTestRunnable::run]"
                         << "Failed to parse latency double for:"
                         << peerData.host;
                peerData.isValid = false;
                peerData.latency = -1;
            }
        } else {
            qDebug() << "[PeerTestRunnable::run]"
                     << "No latency match in ping output for:"
                     << peerData.host;
            peerData.isValid = false;
        }
    } else {
            qDebug() << "[PeerTestRunnable::run]"
                     << "Ping process failed or exited abnormally for:"
                     << peerData.host
                     << "ExitCode:" << pingProcess.exitCode()
                     << "ExitStatus:" << pingProcess.exitStatus();
         peerData.isValid = false;
    }

    qDebug() << "[PeerTestRunnable::run]"
        << "Emitting peerTested signal - host:"
        << peerData.host
        << "isValid:" << peerData.isValid
        << "latency:" << peerData.latency;
    emit peerTested(peerData);
}

/**
 * @brief Exports the given list of peers to a CSV file
 * @param fileName The full path to the CSV file to be created/overwritten
 * @param peerList The list of peers to export
 * @return true if the export was successful, false otherwise
 */
bool PeerManager::exportPeersToCsv(const QString& fileName,
                                   const QList<PeerData>& peerList) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[PeerManager::exportPeersToCsv]"
                 << "Could not open file for writing:"
                 << fileName << file.errorString();
        // Note: Cannot emit 'error' signal directly here as this function might
        // be called from different contexts.  Consider returning an error code
        // or string instead if more detailed error handling is needed upstream.
        return false;
    }

    QTextStream out(&file);
    out << "\"Host\",\"Latency (ms)\",\"Valid?\"\n";

    for (const PeerData& peer : peerList) {
        QString latencyStr;
        // Determine latency string based solely on PeerData
        if (peer.latency < -1) {
            // Assuming latency < -1 might indicate some other failure state,
            // treat as Failed
             latencyStr = "Failed";
        } else if (peer.latency == -1) {
            // latency == -1 indicates not tested
             latencyStr = "Not Tested";
        } else { // latency >= 0 is a valid measurement
            latencyStr = QString::number(peer.latency);
        }

        QString validityStr = "";
        // Determine validity string based solely on PeerData, only if tested
        if (peer.latency != -1) {
             // Only show validity if the peer was actually tested (latency is
             // not -1)
             validityStr = peer.isValid ? "yes" : "no";
        }

        out << "\"" << peer.host << "\","
            << "\"" << latencyStr << "\","
            << "\"" << validityStr << "\"\n";
    }

    file.close();
    qDebug() << "[PeerManager::exportPeersToCsv]"
             << "Successfully exported"
             << peerList.count() << "peers to" << fileName;
    return true;
}

/**
 * @brief Constructor for PeerManager
 * @param settings Application settings
 * @param debugMode Whether to enable debug output
 * @param parent Parent QObject
 */
PeerManager::PeerManager(std::shared_ptr<QSettings> settings,
                         bool debugMode,
                         QObject *parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
    , threadPool(new QThreadPool(this))
    , cancelTestsFlag(0)
    , debugMode(debugMode)
    , settings(settings) {

    qRegisterMetaType<PeerData>("PeerData");
    qRegisterMetaType<QList<PeerData>>("QList<PeerData>");

    connect(networkManager, &QNetworkAccessManager::finished,
            this, &PeerManager::handleNetworkResponse);

    threadPool->setMaxThreadCount(5);
    qDebug() << "[PeerManager] Thread pool initialized with max"
             << threadPool->maxThreadCount()
             << "threads.";
}

/**
 * @brief Destructor for PeerManager
 */
PeerManager::~PeerManager() {
    qDebug() << "[PeerManager::~PeerManager] Cleaning up...";
    cancelTests();
    threadPool->clear();
    qDebug() << "[PeerManager::~PeerManager]"
             << "Waiting for active tests to finish...";
    bool allFinished = threadPool->waitForDone(-1);
    qDebug() << "[PeerManager::~PeerManager] All tests finished:"
             << allFinished;
}

/**
 * @brief Sets the proxy to use for peer fetching network requests
 * @param proxy The QNetworkProxy to use
 */
void PeerManager::setPeerFetchProxy(const QNetworkProxy& proxy) {
    if (networkManager) {
        networkManager->setProxy(proxy);
    }
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

    qDebug() << "[PeerManager::testPeer] Submitting test task for:"
             << peer.host;
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
    qDebug() << "[PeerManager::cancelTests]"
             << "Requesting cancellation of all active tests.";
    cancelTestsFlag.storeRelease(1);
    threadPool->clear();
    qDebug() << "[PeerManager::cancelTests]"
             << "Cancellation flag set and thread pool queue cleared.";
}

/**
 * @brief Extracts a resource file to a specified location
 * @param resourcePath Path to the resource file in Qt's resource system
 * @param outputPath Path where the resource should be extracted
 * @return true if extraction was successful, false otherwise
 * @details If the resource has .sh extension, the output file
 * will be made executable
 */
bool PeerManager::extractResource(const QString& resourcePath,
                                  const QString& outputPath) {
    QFile resourceFile(resourcePath);
    if (!resourceFile.open(QIODevice::ReadOnly)) {
        qDebug() << "[PeerManager::extractResource] Failed to open resource:"
                 << resourcePath;
        return false;
    }

    QFile outputFile(outputPath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        qDebug() << "[PeerManager::extractResource]"
                 << "Failed to create output file:"
                 << outputPath;
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
 * @brief Print a peer data into a stream in a format suitable for adding to the
 * Yggdrasil config.
 * @param stream A text stream to print data to.
 * @param peer A peer data to be printed.
 */
void formatPeer(QTextStream& stream, const PeerData& peer) {
    stream << peer.host << "\n";
}

/**
 * @brief Write a list of peers into an output stream.
 * @param stream An output text stream to write data to.
 * @param peers A list of peers to write.
 */
void writePeers(QTextStream& stream, const QList<PeerData>& peers) {
    for (const auto& peer : peers) {
        formatPeer(stream, peer);
    }
}

/**
 * @brief Updates Yggdrasil configuration with selected peers
 * @param selectedPeers List of peers to include in configuration
 * @return true if configuration was successfully updated
 */
bool PeerManager::updateConfig(const QList<PeerData>& selectedPeers) {
    qDebug() << "[PeerManager::updateConfig] Starting update with"
             << selectedPeers.count() << "peers";

    // Sort peers by latency (lowest first)
    QList<PeerData> sortedPeers = selectedPeers;
    std::sort(sortedPeers.begin(), sortedPeers.end(),
        [](const PeerData& a, const PeerData& b) {
            if (a.isPrivate) {
                if (b.isPrivate) {
                    if (a.isValid && b.isValid) {
                        return a.latency < b.latency;
                    }
                    return a.isValid > b.isValid;
                }
                return true;
            } else if (b.isPrivate) {
                return false;
            }
            if (a.isValid && b.isValid) {
                return a.latency < b.latency;
            }
            return a.isValid > b.isValid;
        });

    QList<PeerData> validPeers;
    validPeers.reserve(sortedPeers.size());
    std::copy_if(sortedPeers.begin(),
                 sortedPeers.end(),
                 std::back_inserter(validPeers),
                 [](const PeerData& p) {
                     return p.isPrivate || p.isValid;
                 });
    qDebug() << "[PeerManager::updateConfig] Valid peers in selection:"
             << validPeers.size();
    for (const auto& peer : validPeers) {
        qDebug() << "[PeerManager::updateConfig]"
                 << "Using peer:"
                 << peer.host;
    }

    // Extract update script to /tmp
    if (!extractResource(":/scripts/update-peers.sh", SCRIPT_PATH)) {
        qDebug() << "[PeerManager::updateConfig]"
                 << "Failed to extract update script";
        return false;
    }

    if (!extractResource(":/polkit/org.yggtray.updatepeers.policy",
                         POLICY_PATH)) {
        // Clean up script if policy extraction fails
        QFile::remove(SCRIPT_PATH);
        qDebug() << "[PeerManager::updateConfig]"
                 << "Failed to extract policy file";
        return false;
    }

    // Create temporary file with peer list
    QTemporaryFile peersFile;
    if (!peersFile.open()) {
        qDebug() << "[PeerManager::updateConfig]"
                 << "Failed to create temporary peers file:"
                 << peersFile.errorString();
        return false;
    }

    // Write peers to temporary file
    QTextStream stream(&peersFile);

    // First try to write only valid peers
    if (validPeers.size() > 0) {
        qDebug() << "[PeerManager::updateConfig] Writing"
                 << validPeers.size()
                 << "valid peers to config (up to"
                 << MAX_PEERS << "will be used)";
        writePeers(stream, validPeers);
    } else {
        // If no valid peers, use all peers as a fallback
        qDebug() << "[PeerManager::updateConfig]"
                 << "Warning: No valid peers found, using all peers as fallback";
        stream.seek(0); // Reset the stream position

        // Use all peers instead, sorted by latency if available
        writePeers(stream, sortedPeers);

        qDebug() << "[PeerManager::updateConfig] Writing"
                 << sortedPeers.count()
                 << "peers to config (up to" << MAX_PEERS << "will be used)";
    }

    stream.flush();

    // Debug: Read back file content to verify it's not empty
    peersFile.seek(0);
    QString fileContent = QString::fromUtf8(peersFile.readAll());
    qDebug() << "[PeerManager::updateConfig] Verifying peers file:"
             << (fileContent.isEmpty() ? "EMPTY!" : "Contains data");
    peersFile.seek(0);  // Reset position for the script to read

    // Execute update script with elevated privileges
    QProcess process;
    QStringList args;
    if (debugMode) {
        args << "sh" << SCRIPT_PATH << "--verbose" << peersFile.fileName();
    } else {
        args << "sh" << SCRIPT_PATH << peersFile.fileName();
    }

    qDebug() << "[PeerManager::updateConfig]"
             << "Executing update script - command: pkexec" << args;
    process.start("pkexec", args);

    if (!process.waitForFinished(SCRIPT_TIMEOUT_MS)) {
        QString errorMsg = "Update script timed out";
        qDebug() << "[PeerManager::updateConfig] Error:" << errorMsg;
        QFile::remove(SCRIPT_PATH);
        QFile::remove(POLICY_PATH);
        emit error(errorMsg);
        return false;
    }

    if (process.exitCode() != 0) {
        QString stdErr = QString::fromUtf8(process.readAllStandardError());
        QString stdOut = QString::fromUtf8(process.readAllStandardOutput());

        // Special case: If the output contains "updated successfully" but exit
        // code is non-zero, consider it a success and ignore the exit code
        if ((stdOut.contains("updated successfully")
             || stdErr.contains("updated successfully"))
            && process.exitCode() == 1) {
            qDebug() << "[PeerManager::updateConfig]"
                     << "Script exited with code 1 but reported success,"
                     << "treating as successful";

            // Clean up temporary files
            QFile::remove(SCRIPT_PATH);
            QFile::remove(POLICY_PATH);
            return true;
        }

        QString errorMsg = "Update script failed with exit code "
            + QString::number(process.exitCode());

        if (!stdErr.isEmpty()) {
            errorMsg += ": " + stdErr.trimmed();
        } else if (!stdOut.isEmpty()) {
            errorMsg += ": " + stdOut.trimmed();
        }

        qDebug() << "[PeerManager::updateConfig] Error:" << errorMsg;
        QFile::remove(SCRIPT_PATH);
        QFile::remove(POLICY_PATH);
        emit error(errorMsg);
        return false;
    }

    // Log the success output
    QString output
        = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        qDebug() << "[PeerManager::updateConfig] Script output:"
                 << output;
    }

    // Clean up temporary files
    QFile::remove(SCRIPT_PATH);
    QFile::remove(POLICY_PATH);
    return true;
}

/**
 * @brief Handles the response from public peers repository
 * @param reply Network reply containing peer list HTML
 */
void PeerManager::handleNetworkResponse(QNetworkReply* reply) {
    QList<PeerData> privatePeersList;
    QString privatePeers
        = settings->value("peer_discovery/private_peers", "").toString();
    qDebug() << "[PeerManager::handleNetworkResponse]"
             << "Private peers: "
             << privatePeers;
    for (auto& p : privatePeers.split(",")) {
        PeerData peer;
        peer.host = p;
        peer.isPrivate = true;
        privatePeersList.append(peer);
    }
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
        if (! privatePeersList.isEmpty()) {
            // Insert private peers into the beginning of the peer list
            // so they will be tested first for the latency.
            privatePeersList += peers;
            peers = privatePeersList;
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
    qDebug() << "[PeerManager::handlePeerTested] Received result for:"
             << peer.host << "on thread" << QThread::currentThreadId();
    emit peerTested(peer);
}
