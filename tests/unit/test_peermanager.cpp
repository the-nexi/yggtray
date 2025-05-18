#include <check.h>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QTemporaryFile>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>
#include "../../src/PeerManager.h"

// Test getHostname logic
START_TEST(test_getHostname_basic)
{
    printf("[PeerManager] test_getHostname_basic: Testing getHostname parsing...\n");
    PeerManager mgr(false, nullptr);
    ck_assert_str_eq(
        mgr.getHostname("tls://[2001:db8::1]:1234").toUtf8().constData(),
        "2001:db8::1"
    );
    ck_assert_str_eq(
        mgr.getHostname("tcp://192.168.1.1:1234").toUtf8().constData(),
        "192.168.1.1"
    );
    ck_assert_str_eq(
        mgr.getHostname("quic://example.com:1234").toUtf8().constData(),
        "example.com"
    );
    ck_assert_str_eq(
        mgr.getHostname("invalidstring").toUtf8().constData(),
        ""
    );
}
END_TEST

// Test exportPeersToCsv logic
START_TEST(test_exportPeersToCsv_basic)
{
    printf("[PeerManager] test_exportPeersToCsv_basic: Testing exportPeersToCsv output...\n");
    PeerManager mgr(false, nullptr);
    QList<PeerData> peers;
    PeerData p1;
    p1.host = "peer1";
    p1.latency = 10;
    p1.isValid = true;
    PeerData p2;
    p2.host = "peer2";
    p2.latency = -1;
    p2.isValid = false;
    peers << p1 << p2;

    QTemporaryFile tmpFile;
    ck_assert(tmpFile.open());
    QString fileName = tmpFile.fileName();
    tmpFile.close(); // PeerManager will overwrite

    ck_assert(mgr.exportPeersToCsv(fileName, peers));

    QFile file(fileName);
    ck_assert(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    ck_assert(content.contains("peer1"));
    ck_assert(content.contains("peer2"));
    ck_assert(content.contains("10"));
    ck_assert(content.contains("Not Tested"));
}
END_TEST

// Mock QNetworkReply for signal test
class DummyReply : public QNetworkReply {
    QByteArray data;
public:
    DummyReply(const QByteArray& html, QObject* parent = nullptr)
        : QNetworkReply(parent), data(html)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setHeader(QNetworkRequest::ContentTypeHeader, "text/html");
        setFinished(true);
        setError(NoError, QString());
    }
    void abort() override {}
    qint64 readData(char* buffer, qint64 maxlen) override {
        qint64 toRead = qMin(maxlen, qint64(data.size()));
        memcpy(buffer, data.constData(), toRead);
        data.remove(0, toRead);
        return toRead;
    }
    qint64 bytesAvailable() const override { return data.size() + QIODevice::bytesAvailable(); }
    void setFinished(bool f) { QNetworkReply::setFinished(f); }
    void setError(QNetworkReply::NetworkError code, const QString& str) { QNetworkReply::setError(code, str); }
};

// Test peersDiscovered signal emission after HTML parsing
START_TEST(test_peersDiscovered_signal)
{
    printf("[PeerManager] test_peersDiscovered_signal: Testing peersDiscovered signal after HTML parsing...\n");
    PeerManager mgr(false, nullptr);

    // Prepare HTML with two peer URIs
    QByteArray html =
        "<html><body>"
        "<td>tls://[2001:db8::1]:1234</td>"
        "<td>tcp://192.168.1.1:1234</td>"
        "</body></html>";
    DummyReply* reply = new DummyReply(html);

    QSignalSpy spy(&mgr, SIGNAL(peersDiscovered(QList<PeerData>)));

    // Call the slot directly
    QMetaObject::invokeMethod(
        &mgr,
        "handleNetworkResponse",
        Qt::DirectConnection,
        Q_ARG(QNetworkReply*, reply)
    );

    // Should have emitted peersDiscovered once
    ck_assert_int_eq(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    QList<PeerData> peers =
        qvariant_cast<QList<PeerData>>(args.at(0));
    ck_assert_int_eq(peers.size(), 2);
    ck_assert(peers[0].host.contains("tls://[2001:db8::1]:1234"));
    ck_assert(peers[1].host.contains("tcp://192.168.1.1:1234"));

    reply->deleteLater();
}
END_TEST

// Test error signal emission on network failure
START_TEST(test_error_signal_network_failure)
{
    printf("[PeerManager] test_error_signal_network_failure: Testing error signal on network failure...\n");
    PeerManager mgr(false, nullptr);

    DummyReply* reply = new DummyReply("", nullptr);
    reply->setError(QNetworkReply::ConnectionRefusedError, "Connection refused");
    reply->setFinished(true);

    QSignalSpy spy(&mgr, SIGNAL(error(QString)));

    QMetaObject::invokeMethod(
        &mgr,
        "handleNetworkResponse",
        Qt::DirectConnection,
        Q_ARG(QNetworkReply*, reply)
    );

    ck_assert_int_eq(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QString errMsg = args.at(0).toString();
    ck_assert(errMsg.contains("Connection refused") || errMsg.contains("network error"));

    reply->deleteLater();
}
END_TEST

// Test cancellation logic: all peer tests should be canceled
START_TEST(test_cancelTests_cancels_all)
{
    printf("[PeerManager] test_cancelTests_cancels_all: Testing that cancelTests cancels all peer checks...\n");
    PeerManager mgr(false, nullptr);

    QSignalSpy spy(&mgr, SIGNAL(peerTested(PeerData)));

    // Prepare 10 fake peers
    QList<PeerData> peers;
    for (int i = 0; i < 10; ++i) {
        PeerData p;
        p.host = QString("peer%1").arg(i);
        p.latency = -1;
        p.isValid = false;
        peers << p;
    }

    // Start peer tests (these will run in thread pool)
    for (const PeerData& p : peers) {
        mgr.testPeer(p);
    }

    // Cancel all tests immediately
    mgr.cancelTests();

    // Wait a bit to allow threads to process cancellation
    QTest::qWait(200);

    // There should be 0 or very few peerTested signals (depending on thread timing)
    // The main check: no more than 1-2 signals, and certainly not 10
    int count = spy.count();
    printf("[PeerManager] peerTested signals emitted after cancel: %d\n", count);
    ck_assert(count < 3);
}
END_TEST

// Test: handleNetworkResponse with empty HTML (no peers found)
START_TEST(test_peersDiscovered_empty_list)
{
    printf("[PeerManager] test_peersDiscovered_empty_list: Testing peersDiscovered with empty peer list...\n");
    PeerManager mgr(false, nullptr);

    QByteArray html = "<html><body></body></html>"; // No <td> tags
    DummyReply* reply = new DummyReply(html);

    QSignalSpy spy(&mgr, SIGNAL(peersDiscovered(QList<PeerData>)));

    QMetaObject::invokeMethod(
        &mgr,
        "handleNetworkResponse",
        Qt::DirectConnection,
        Q_ARG(QNetworkReply*, reply)
    );

    ck_assert_int_eq(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QList<PeerData> peers =
        qvariant_cast<QList<PeerData>>(args.at(0));
    ck_assert_int_eq(peers.size(), 0);

    reply->deleteLater();
}
END_TEST

// Test: handleNetworkResponse with unreachable server (network error)
START_TEST(test_error_signal_peerlist_unreachable)
{
    printf("[PeerManager] test_error_signal_peerlist_unreachable: Testing error signal when peer list server is unreachable...\n");
    PeerManager mgr(false, nullptr);

    DummyReply* reply = new DummyReply("", nullptr);
    reply->setError(QNetworkReply::HostNotFoundError, "Host not found");
    reply->setFinished(true);

    QSignalSpy spy(&mgr, SIGNAL(error(QString)));

    QMetaObject::invokeMethod(
        &mgr,
        "handleNetworkResponse",
        Qt::DirectConnection,
        Q_ARG(QNetworkReply*, reply)
    );

    ck_assert_int_eq(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QString errMsg = args.at(0).toString();
    ck_assert(errMsg.contains("Host not found") || errMsg.contains("network error"));

    reply->deleteLater();
}
END_TEST

Suite* peermanager_suite(void)
{
    Suite* s = suite_create("PeerManager");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_getHostname_basic);
    tcase_add_test(tc, test_exportPeersToCsv_basic);
    tcase_add_test(tc, test_peersDiscovered_signal);
    tcase_add_test(tc, test_error_signal_network_failure);
    tcase_add_test(tc, test_cancelTests_cancels_all);
    tcase_add_test(tc, test_peersDiscovered_empty_list);
    tcase_add_test(tc, test_error_signal_peerlist_unreachable);

    suite_add_tcase(s, tc);
    return s;
}

/* No main here: suite will be registered from the global test runner. */
