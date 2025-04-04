#ifndef PEERDISCOVERYDIALOG_H
#define PEERDISCOVERYDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QCloseEvent>
#include "PeerManager.h"

/**
 * @class PeerDiscoveryDialog
 * @brief Dialog for discovering and managing Yggdrasil peers
 */
class PeerDiscoveryDialog : public QDialog {
    Q_OBJECT

public:
    explicit PeerDiscoveryDialog(bool debugMode = false, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPeersDiscovered(const QList<PeerData>& peers);
    void onPeerTested(const PeerData& peer);
    void onError(const QString& message);
    void onRefreshClicked();
    void onTestClicked();
    void onApplyClicked();
    void startNextPeerTest(int index);

private:
    void setupUi();
    void setupConnections();
    void stopTesting();

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
