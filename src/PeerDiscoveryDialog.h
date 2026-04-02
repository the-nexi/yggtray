#ifndef PEERDISCOVERYDIALOG_H
#define PEERDISCOVERYDIALOG_H

#include <memory>
#include <QCloseEvent>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTranslator>
#include <QSettings>

#include "PeerManager.h"

/**
 * @brief A special class to handle validity column in the peer table.
 */
class ValidityItem : public QTableWidgetItem {
public:
    ValidityItem(bool isValid)
        : QTableWidgetItem(isValid ? QObject::tr("yes") : QObject::tr("no")) {
        // Do nothing.
    }
};

// Subclass to handle latency sorting with -1 treated as slowest
class LatencyItem : public QTableWidgetItem {
public:
    /**
     * @brief Make a LatencyItem instance.
     * @param latency Peer latency.  -1 means that the peer latency
     * is not tested yet.
     * @param isValid Peer validity.
     * @param isTested Whether the peer latency was tested or not.
     */
    LatencyItem(int latency = -1, bool isValid = false, bool isTested = false)
        : QTableWidgetItem(latency >= 0 ? QString::number(latency) : QString("-")),
          m_latency(latency),
          m_isValid(isValid),
          m_isTested(isTested)
    {
        if (m_isTested) {
            QColor backgroundColor
                = m_isValid ? QColor(220, 255, 220) : QColor(255, 220, 220);
            setData(Qt::BackgroundRole, backgroundColor);
            setData(Qt::ForegroundRole, QColor(0, 0, 0));
        }
    }

    bool operator<(const QTableWidgetItem &other) const override {
        const LatencyItem* otherItem = dynamic_cast<const LatencyItem*>(&other);
        int otherLatency
            = otherItem ? otherItem->m_latency : other.text().toInt();

        if (m_latency < 0 && otherLatency >= 0) return false;
        if (otherLatency < 0 && m_latency >= 0) return true;
        return m_latency < otherLatency;
    }

    int latency() const { return m_latency; }
    bool isValid() const { return m_isValid; }
    bool isTested() const { return m_isTested; }

private:
    int m_latency;
    bool m_isValid;
    bool m_isTested;
};

/**
 * @class PeerDiscoveryTableView
 * @brief Custom QTableView to handle Yggdrasil peers.
 */
class PeerDiscoveryTableWidget : public QTableWidget
{
public:
    PeerDiscoveryTableWidget(QWidget* parent = Q_NULLPTR);

protected:
    void keyPressEvent(QKeyEvent *event);
};

/**
 * @class PeerDiscoveryDialog
 * @brief Dialog for discovering and managing Yggdrasil peers
 */
class PeerDiscoveryDialog : public QDialog {
    Q_OBJECT

public:
    explicit PeerDiscoveryDialog(std::shared_ptr<QSettings> settings,
                                 bool debugMode = false,
                                 QWidget *parent = nullptr);

    /**
     * @brief Sets the proxy to use for peer fetching network requests
     * @param proxy The QNetworkProxy to use
     */
    void setPeerFetchProxy(const QNetworkProxy& proxy);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPeersDiscovered(const QList<PeerData>& peers);
    void onPeerTested(const PeerData& peer);
    void onError(const QString& message);
    void onRefreshClicked();
    void onTestClicked();
    void onApplyClicked();
    void onExportClicked();

    /**
     * @brief Show the proxy configuration dialog
     */
    void onProxyConfigClicked();

    /**
     * @brief Show the private peers configuration dialog.
     */
    void onPrivatePeersClicked();

private:
    void setupUi();
    void setupConnections();
    void stopTesting();
    void resetTableUI();
    void setRowColor(int row, bool isValid, bool isTested);

    std::shared_ptr<QSettings> settings;

    PeerManager* peerManager;
    QPushButton* refreshButton;
    QPushButton* testButton;
    QPushButton* applyButton;
    QPushButton* exportButton;
    QPushButton* proxyButton;

    /**
     * @brief A button that opens the private peers manager.
     */
    QPushButton* privatePeersButton;

    QTableWidget* peerTable;
    QProgressBar* progressBar;
    QLabel* statusLabel;
    QList<PeerData> peerList;
    int testedPeers;
    int totalPeers;
    bool isTesting;
};

#endif // PEERDISCOVERYDIALOG_H
