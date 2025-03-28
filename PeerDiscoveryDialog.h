#ifndef PEERDISCOVERYDIALOG_H
#define PEERDISCOVERYDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QLabel>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDebug>
#include <QTimer>
#include "PeerManager.h"

/**
 * @class PeerDiscoveryDialog
 * @brief Dialog for discovering and managing Yggdrasil peers
 */
class PeerDiscoveryDialog : public QDialog {
    Q_OBJECT

public:
    explicit PeerDiscoveryDialog(bool debugMode = false, QWidget *parent = nullptr)
        : QDialog(parent)
        , peerManager(new PeerManager(debugMode, this))
        , testedPeers(0)
        , totalPeers(0)
        , isTesting(false) {
        setWindowTitle(tr("Peer Discovery"));
        setupUi();
        setupConnections();
    }

protected:
    // Override closeEvent to handle cancellation of ongoing tests
    void closeEvent(QCloseEvent *event) override {
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

private slots:
    void onPeersDiscovered(const QList<PeerData>& peers) {
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
    }

    void onPeerTested(const PeerData& peer) {
        testedPeers++;
        progressBar->setValue((testedPeers * 100) / totalPeers);

        // Find the peer in the peerList and update it
        for (int i = 0; i < peerList.size(); ++i) {
            if (peerList[i].host == peer.host) {
                // Update the peer in our list with the tested results
                peerList[i].latency = peer.latency;
                peerList[i].isValid = peer.isValid;
                qDebug() << "Updated peer in peerList:" << peer.host 
                         << "isValid:" << peer.isValid 
                         << "latency:" << peer.latency;
                
                // Update the UI representation
                if (i < peerTable->rowCount()) {
                    peerTable->setItem(i, 1, new QTableWidgetItem(
                        peer.latency < 0 ? tr("Failed") : QString::number(peer.latency) + "ms"
                    ));
                    peerTable->setItem(i, 2, new QTableWidgetItem("-")); // Remove speed display
                    peerTable->setItem(i, 3, new QTableWidgetItem(
                        peer.isValid ? tr("Valid") : tr("Invalid")
                    ));
                    
                    // Color code based on status
                    for (int col = 0; col < peerTable->columnCount(); ++col) {
                        QTableWidgetItem* item = peerTable->item(i, col);
                        item->setBackground(peer.isValid ? QColor(220, 255, 220) : QColor(255, 220, 220));
                        item->setForeground(QColor(0, 0, 0));
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
            refreshButton->setEnabled(true); // Re-enable refresh button when testing completes
            isTesting = false;
        }
    }

    void onError(const QString& message) {
        QMessageBox::warning(this, tr("Error"), message);
    }

    void onRefreshClicked() {
        statusLabel->setText(tr("Fetching peers..."));
        testButton->setEnabled(false);
        applyButton->setEnabled(false);
        progressBar->setValue(0);
        peerManager->fetchPeers();
    }

    void onTestClicked() {
        if (isTesting) {
            // If we're testing, then the button acts as a stop button
            stopTesting();
            return;
        }

        testedPeers = 0;
        totalPeers = peerList.count();
        progressBar->setValue(0);
        applyButton->setEnabled(false);
        // Disable the refresh button during testing
        refreshButton->setEnabled(false);
        statusLabel->setText(tr("Testing peers: 0/%1").arg(totalPeers));
        
        // Change button text to 'Stop'
        testButton->setText(tr("Stop"));
        isTesting = true;
        
        // Reset any previous cancellation before starting new tests
        peerManager->resetCancellation();
        
        // Schedule peer tests to be started one by one using a timer
        // rather than queuing them all at once
        startNextPeerTest(0);
    }
    
    void startNextPeerTest(int index) {
        if (!isTesting || index >= peerList.size()) {
            return;
        }
        
        // Test the current peer
        peerManager->testPeer(peerList[index]);
        
        // Schedule the next peer test after a short delay
        if (index + 1 < peerList.size()) {
            QTimer::singleShot(100, this, [this, index]() {
                startNextPeerTest(index + 1);
            });
        }
    }

    void onApplyClicked() {
        QList<PeerData> selectedPeers;
        auto selectionModel = peerTable->selectionModel();
        
        // Print the current state of peerList
        qDebug() << "\nCurrent peerList state:";
        for (const auto& peer : peerList) {
            qDebug() << "Peer in list:" << peer.host << "isValid:" << peer.isValid << "latency:" << peer.latency;
        }
        
        if (selectionModel->hasSelection()) {
            // Get selected rows
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
            // If no selection, use all rows
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

private:
    void setupUi() {
        auto layout = new QVBoxLayout(this);
        
        // Buttons layout
        auto buttonLayout = new QHBoxLayout();
        refreshButton = new QPushButton(tr("Refresh"), this);
        testButton = new QPushButton(tr("Test"), this);
        applyButton = new QPushButton(tr("Apply"), this);
        testButton->setEnabled(false);
        applyButton->setEnabled(false);
        
        buttonLayout->addWidget(refreshButton);
        buttonLayout->addWidget(testButton);
        buttonLayout->addWidget(applyButton);
        buttonLayout->addStretch();
        
        // Table
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
        
        // Progress bar
        progressBar = new QProgressBar(this);
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        
        // Status label
        statusLabel = new QLabel(tr("Ready"), this);
        
        layout->addLayout(buttonLayout);
        layout->addWidget(peerTable);
        layout->addWidget(progressBar);
        layout->addWidget(statusLabel);
        
        resize(600, 400);
    }

    void setupConnections() {
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
    }

    void stopTesting() {
        if (!isTesting) return;
        
        // Signal PeerManager to stop the tests
        peerManager->cancelTests();
        
        // Reset the UI
        testButton->setText(tr("Test"));
        isTesting = false;
        
        // Re-enable refresh button
        refreshButton->setEnabled(true);
        
        // If we've tested at least some peers, enable the Apply button
        if (testedPeers > 0) {
            applyButton->setEnabled(true);
        }
        
        statusLabel->setText(tr("Testing canceled"));
    }

    PeerManager* peerManager;
    QPushButton* refreshButton;
    QPushButton* testButton;
    QPushButton* applyButton;
    QTableWidget* peerTable;
    QProgressBar* progressBar;
    QLabel* statusLabel;
    QList<PeerData> peerList;
    int testedPeers;
    int totalPeers;
    bool isTesting;
};

#endif // PEERDISCOVERYDIALOG_H
