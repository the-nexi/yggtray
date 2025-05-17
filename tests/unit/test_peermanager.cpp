#include <check.h>
#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QTemporaryFile>
#include "../../src/PeerManager.h"

// Test getHostname logic
START_TEST(test_getHostname_basic)
{
    PeerManager mgr(false, nullptr);
    ck_assert_str_eq(mgr.getHostname("tls://[2001:db8::1]:1234").toUtf8().constData(), "2001:db8::1");
    ck_assert_str_eq(mgr.getHostname("tcp://192.168.1.1:1234").toUtf8().constData(), "192.168.1.1");
    ck_assert_str_eq(mgr.getHostname("quic://example.com:1234").toUtf8().constData(), "example.com");
    ck_assert_str_eq(mgr.getHostname("invalidstring").toUtf8().constData(), "");
}
END_TEST

// Test exportPeersToCsv logic
START_TEST(test_exportPeersToCsv_basic)
{
    PeerManager mgr(false, nullptr);
    QList<PeerData> peers;
    PeerData p1; p1.host = "peer1"; p1.latency = 10; p1.isValid = true;
    PeerData p2; p2.host = "peer2"; p2.latency = -1; p2.isValid = false;
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

Suite* peermanager_suite(void)
{
    Suite* s = suite_create("PeerManager");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_getHostname_basic);
    tcase_add_test(tc, test_exportPeersToCsv_basic);

    suite_add_tcase(s, tc);
    return s;
}

/* No main here: suite will be registered from the global test runner. */
