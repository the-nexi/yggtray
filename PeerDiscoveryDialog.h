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
#include "PeerManager.h"

/**
 * @class PeerDiscoveryDialog
 * @brief Dialog for discovering and managing Yggdrasil peers
 */
class PeerDiscoveryDialog : public QDialog {
    Q_OBJECT

public:
    explicit PeerDiscoveryDialog(QWidget *parent = nullptr)
        : QDialog(parent)
        , peerManager(new PeerManager(this))
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

        // Find and update the peer in the table
        for (int i = 0; i < peerTable->rowCount(); ++i) {
            if (peerTable->item(i, 0)->text() == peer.host) {
                peerTable->setItem(i, 1, new QTableWidgetItem(
                    peer.latency < 0 ? tr("Failed") : QString::number(peer.latency) + "ms"
                ));
                peerTable->setItem(i, 2, new QTableWidgetItem(
                    peer.speed < 0 ? tr("Failed") : QString::number(peer.speed, 'f', 2) + " Mbps"
                ));
                peerTable->setItem(i, 3, new QTableWidgetItem(
                    peer.isValid ? tr("Valid") : tr("Invalid")
                ));

                // Color code based on status - using better contrast
                for (int col = 0; col < peerTable->columnCount(); ++col) {
                    QTableWidgetItem* item = peerTable->item(i, col);
                    if (peer.isValid) {
                        // Use a lighter green background with black text for better readability
                        item->setBackground(QColor(220, 255, 220));
                        item->setForeground(QColor(0, 0, 0));
                    } else {
                        // Use a lighter red background with black text
                        item->setBackground(QColor(255, 220, 220));
                        item->setForeground(QColor(0, 0, 0));
                    }
                }
                break;
            }
        }

        statusLabel->setText(tr("Testing peers: %1/%2").arg(testedPeers).arg(totalPeers));
        
        if (testedPeers == totalPeers) {
            statusLabel->setText(tr("Testing complete"));
            applyButton->setEnabled(true);
            testButton->setText(tr("Test"));
            testButton->setEnabled(true);
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
        statusLabel->setText(tr("Testing peers: 0/%1").arg(totalPeers));
        
        // Change button text to 'Stop'
        testButton->setText(tr("Stop"));
        isTesting = true;
        
        // Now we just need to start the tests in a non-blocking way
        // The PeerManager will handle the threading
        for (const auto& peer : peerList) {
            peerManager->testPeer(peer);
        }
    }

    void onApplyClicked() {
        QList<PeerData> selectedPeers;
        QSet<int> selectedRows;
        auto selectionModel = peerTable->selectionModel();
        
        if (selectionModel->hasSelection()) {
            auto selectedRanges = selectionModel->selectedRows();
            for (const auto& range : selectedRanges) {
                selectedRows.insert(range.row());
            }
        }

        // If no specific selection, use all valid peers
        if (selectedRows.isEmpty()) {
            for (int i = 0; i < peerList.size(); i++) {
                if (peerList[i].isValid) {
                    selectedPeers.append(peerList[i]);
                }
            }
        } else {
            for (int row : selectedRows) {
                if (row < peerList.size()) {
                    selectedPeers.append(peerList[row]);
                }
            }
        }

        // Make sure we have at least one valid peer
        bool hasValidPeer = false;
        for (const auto& peer : selectedPeers) {
            if (peer.isValid) {
                hasValidPeer = true;
                break;
            }
        }

        if (!hasValidPeer) {
            QMessageBox::warning(this, tr("Warning"),
                               tr("No valid peers selected"));
            return;
        }

        if (peerManager->updateConfig(selectedPeers)) {
            QMessageBox::information(this, tr("Success"),
                                   tr("Configuration updated with %1 peers")
                                   .arg(selectedPeers.count()));
            accept();
        } else {
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
            tr("Speed"),
            tr("Status")
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
