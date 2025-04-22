/**
 * @file PeerDiscoveryDialog.cpp
 * @brief Implementation file for the PeerDiscoveryDialog class.
 *
 * Contains implementation of dialog for discovering and managing Yggdrasil peers.
 */

#include "PeerDiscoveryDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>
#include <QFileDialog>
#include <QBrush>      

/**
 * @brief Constructor for PeerDiscoveryDialog
 * @param debugMode Whether to enable debug output
 * @param parent Parent widget
 */
PeerDiscoveryDialog::PeerDiscoveryDialog(bool debugMode, QWidget *parent)
    : QDialog(parent)
    , peerManager(new PeerManager(debugMode, this))
    , testedPeers(0)
    , totalPeers(0)
    , isTesting(false) {
    setWindowTitle(tr("Peer Discovery"));
    setupUi();
    setupConnections();
}

/**
 * @brief Handle close event to properly cancel ongoing tests
 * @param event Close event
 */
void PeerDiscoveryDialog::closeEvent(QCloseEvent *event) {
    if (isTesting) {
        // Ask user if they want to cancel the tests
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Cancel Testing"),
            tr("Testing is in progress. Cancel and close the dialog?"),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            // Stop the testing
            stopTesting();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}

/**
 * @brief Set up the user interface
 */
void PeerDiscoveryDialog::setupUi() {
    auto layout = new QVBoxLayout(this);

    auto buttonLayout = new QHBoxLayout();
    refreshButton = new QPushButton(tr("Refresh"), this);
    testButton = new QPushButton(tr("Test"), this);
    applyButton = new QPushButton(tr("Apply"), this);
    exportButton = new QPushButton(tr("Export CSV"), this);
    testButton->setEnabled(false);
    applyButton->setEnabled(false);
    exportButton->setEnabled(false);
    
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addWidget(testButton);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(exportButton);
    buttonLayout->addStretch();

    peerTable = new QTableWidget(this);
    peerTable->setColumnCount(4);
    peerTable->setHorizontalHeaderLabels({
        tr("Host"),
        tr("Latency"),
        tr("Status"),
        tr("Valid")
    });
    peerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    peerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    peerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    peerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);

    statusLabel = new QLabel(tr("Ready"), this);

    layout->addLayout(buttonLayout);
    layout->addWidget(peerTable);
    layout->addWidget(progressBar);
    layout->addWidget(statusLabel);
    
    resize(600, 400);
}

/**
 * @brief Set up signal-slot connections
 */
void PeerDiscoveryDialog::setupConnections() {
    connect(refreshButton, &QPushButton::clicked,
            this, &PeerDiscoveryDialog::onRefreshClicked);
    connect(testButton, &QPushButton::clicked,
            this, &PeerDiscoveryDialog::onTestClicked);
    connect(applyButton, &QPushButton::clicked,
            this, &PeerDiscoveryDialog::onApplyClicked);
    
    connect(peerManager, &PeerManager::peersDiscovered,
            this, &PeerDiscoveryDialog::onPeersDiscovered);
    connect(peerManager, &PeerManager::peerTested,
            this, &PeerDiscoveryDialog::onPeerTested);
    connect(peerManager, &PeerManager::error,
            this, &PeerDiscoveryDialog::onError);

    connect(exportButton, &QPushButton::clicked,
            this, &PeerDiscoveryDialog::onExportClicked);
}

/**
 * @brief Resets the table UI to its initial state before testing.
 */
void PeerDiscoveryDialog::resetTableUI() {
    for (int i = 0; i < peerTable->rowCount(); ++i) {
        QTableWidgetItem* latencyItem = peerTable->item(i, 1);
        if (!latencyItem) {
            latencyItem = new QTableWidgetItem("-");
            peerTable->setItem(i, 1, latencyItem);
        } else {
            latencyItem->setText("-");
        }
        
        QTableWidgetItem* statusItem = peerTable->item(i, 2);
         if (!statusItem) {
            statusItem = new QTableWidgetItem("-");
            peerTable->setItem(i, 2, statusItem);
        } else {
            statusItem->setText("-");
        }

        QTableWidgetItem* validityItem = peerTable->item(i, 3);
        if (!validityItem) {
            validityItem = new QTableWidgetItem(tr("Not Tested"));
            peerTable->setItem(i, 3, validityItem);
        } else {
            validityItem->setText(tr("Not Tested"));
        }

        for (int col = 0; col < peerTable->columnCount(); ++col) {
            QTableWidgetItem* item = peerTable->item(i, col);
            if (item) {
                item->setData(Qt::BackgroundRole, QVariant()); 
                item->setData(Qt::ForegroundRole, QVariant()); 
            }
        }
    }
}


/**
 * @brief Stop ongoing peer testing
 */
void PeerDiscoveryDialog::stopTesting() {
    if (!isTesting) return;

    peerManager->cancelTests();

    testButton->setText(tr("Test"));
    isTesting = false;

    refreshButton->setEnabled(true);

    if (testedPeers > 0) {
        applyButton->setEnabled(true);
    }
    
    statusLabel->setText(tr("Testing canceled"));
    exportButton->setEnabled(!peerList.isEmpty());
}


/**
 * @brief Handle discovered peers data
 * @param peers List of discovered peers
 */
void PeerDiscoveryDialog::onPeersDiscovered(const QList<PeerData>& peers) {
    peerTable->clearContents();
    peerTable->setRowCount(peers.count());
    peerList = peers;
    testedPeers = 0;
    totalPeers = peers.count();
    
    for (int i = 0; i < peers.count(); ++i) {
        const auto& peer = peers[i];
        peerTable->setItem(i, 0, new QTableWidgetItem(peer.host));
        peerTable->setItem(i, 1, new QTableWidgetItem("-"));
        peerTable->setItem(i, 2, new QTableWidgetItem("-"));
        peerTable->setItem(i, 3, new QTableWidgetItem(tr("Not Tested")));
    }

    statusLabel->setText(tr("Found %1 peers").arg(peers.count()));
    testButton->setEnabled(true);
    exportButton->setEnabled(peers.count() > 0);
}

/**
 * @brief Handle peer test results
 * @param peer The tested peer with updated information
 */
void PeerDiscoveryDialog::onPeerTested(const PeerData& peer) {
    testedPeers++;
    progressBar->setValue((testedPeers * 100) / totalPeers);

    for (int i = 0; i < peerList.size(); ++i) {
        if (peerList[i].host == peer.host) {
            peerList[i].latency = peer.latency;
            peerList[i].isValid = peer.isValid;
            qDebug() << "Updated peer in peerList:" << peer.host 
                     << "isValid:" << peer.isValid 
                     << "latency:" << peer.latency;
            
            if (i < peerTable->rowCount()) {
                peerTable->setItem(i, 1, new QTableWidgetItem(
                    peer.latency < 0 ? tr("Failed") : QString::number(peer.latency) + "ms"
                ));
                peerTable->setItem(i, 2, new QTableWidgetItem("-")); // Remove speed display
                peerTable->setItem(i, 3, new QTableWidgetItem(
                    peer.isValid ? tr("Valid") : tr("Invalid")
                ));
                
                for (int col = 0; col < peerTable->columnCount(); ++col) {
                    QTableWidgetItem* item = peerTable->item(i, col);
                    if (item) { 
                        QColor backgroundColor = peer.isValid ? QColor(220, 255, 220) : QColor(255, 220, 220);
                        QColor foregroundColor = QColor(0, 0, 0); 
                        item->setData(Qt::BackgroundRole, backgroundColor);
                        item->setData(Qt::ForegroundRole, foregroundColor);
                    }
                }
            }
            break;
        }
    }

    statusLabel->setText(tr("Testing peers: %1/%2").arg(testedPeers).arg(totalPeers));
    
    if (testedPeers == totalPeers) {
        // Debug: Print the state of the peerList after all tests completed
        qDebug() << "\nCurrent peerList state:";
        for (int i = 0; i < peerList.size() && i < 50; ++i) { // Limit to 50 to avoid flooding log
            qDebug() << "Peer in list:" << peerList[i].host 
                     << "isValid:" << peerList[i].isValid 
                     << "latency:" << peerList[i].latency;
        }
        
        statusLabel->setText(tr("Testing complete"));
        applyButton->setEnabled(true);
        testButton->setText(tr("Test"));
        testButton->setEnabled(true);
        refreshButton->setEnabled(true); 
        exportButton->setEnabled(!peerList.isEmpty()); 
        isTesting = false;
    }
}

/**
 * @brief Handle error messages
 * @param message Error message
 */
void PeerDiscoveryDialog::onError(const QString& message) {
    QMessageBox::warning(this, tr("Error"), message);
}

/**
 * @brief Handle refresh button click
 */
void PeerDiscoveryDialog::onRefreshClicked() {
    statusLabel->setText(tr("Fetching peers..."));
    testButton->setEnabled(false);
    applyButton->setEnabled(false);
    exportButton->setEnabled(false); 
    progressBar->setValue(0);
    peerManager->fetchPeers();
}

/**
 * @brief Handle test button click
 */
void PeerDiscoveryDialog::onTestClicked() {
    if (isTesting) {
        stopTesting();
        return; 
    }

    if (peerList.isEmpty()) {
        statusLabel->setText(tr("No peers to test. Please refresh."));
        return;
    }

    resetTableUI(); 
    testedPeers = 0;
    totalPeers = peerList.count();
    progressBar->setValue(0);
    statusLabel->setText(tr("Testing peers: 0/%1").arg(totalPeers));

    applyButton->setEnabled(false);
    exportButton->setEnabled(false); 
    refreshButton->setEnabled(false); 
    testButton->setText(tr("Stop")); 

    isTesting = true;
    
    peerManager->resetCancellation();
    
    qDebug() << "[PeerDiscoveryDialog::onTestClicked] Starting parallel test for" << totalPeers << "peers.";
    for (const PeerData& peer : peerList) {
        PeerData peerToTest = peer; 
        peerToTest.latency = -1;
        peerToTest.isValid = false; 
        peerManager->testPeer(peerToTest); 
    }
}

/**
 * @brief Handle apply button click
 */
void PeerDiscoveryDialog::onApplyClicked() {
    QList<PeerData> selectedPeers;
    auto selectionModel = peerTable->selectionModel();
    
    // Print the current state of peerList
    qDebug() << "\nCurrent peerList state:";
    for (const auto& peer : peerList) {
        qDebug() << "Peer in list:" << peer.host << "isValid:" << peer.isValid << "latency:" << peer.latency;
    }
    
    if (selectionModel->hasSelection()) {
        auto selectedRanges = selectionModel->selectedRows();
        qDebug() << "\nSelected rows:" << selectedRanges.count();
        for (const auto& range : selectedRanges) {
            if (range.row() < peerList.size()) {
                const PeerData& peer = peerList[range.row()];
                selectedPeers.append(peer);
                qDebug() << "Added selected peer:" << peer.host 
                         << "isValid:" << peer.isValid 
                         << "latency:" << peer.latency
                         << "row:" << range.row();
            }
        }
    } else {
        qDebug() << "\nNo selection, using all" << peerList.size() << "peers";
        selectedPeers = peerList;
        for (const auto& peer : selectedPeers) {
            qDebug() << "Using peer:" << peer.host 
                     << "isValid:" << peer.isValid 
                     << "latency:" << peer.latency;
        }
    }

    if (selectedPeers.isEmpty()) {
        qDebug() << "No peers selected, aborting";
        QMessageBox::warning(this, tr("Warning"),
                           tr("No peers selected"));
        return;
    }

    qDebug() << "Total peers to apply:" << selectedPeers.count() 
             << "Valid peers:" << std::count_if(selectedPeers.begin(), selectedPeers.end(), 
                   [](const PeerData& p) { return p.isValid; });
    if (peerManager->updateConfig(selectedPeers)) {
        qDebug() << "Configuration updated successfully";
        QMessageBox::information(this, tr("Success"),
                   tr("Configuration updated successfully"));
        accept();
    } else {
        qDebug() << "Configuration update failed";
        QMessageBox::critical(this, tr("Error"),
                            tr("Failed to update configuration"));
    }
}

/**
 * @brief Handle export button click
 */
void PeerDiscoveryDialog::onExportClicked() {
    if (peerList.isEmpty()) {
        QMessageBox::information(this, tr("Export CSV"), tr("No peer data to export."));
        return;
    }

    QString defaultFileName = "yggdrasil-peers.csv";
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Peers as CSV"),
                                                    defaultFileName,
                                                    tr("CSV Files (*.csv);;All Files (*)"));

    if (fileName.isEmpty()) {
        return; 
    }

    if (peerManager->exportPeersToCsv(fileName, peerList)) {
        QMessageBox::information(this, tr("Export Successful"),
                                 tr("Peer data successfully exported to %1").arg(fileName));
    } else {
        QMessageBox::warning(this, tr("Export Error"),
                             tr("Failed to export peer data. See logs for details."));
    }
}
