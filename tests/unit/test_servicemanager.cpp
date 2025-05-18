#include <check.h>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include "../../src/ServiceManager.h"
#include "../mocks/MockProcessRunner.h"

START_TEST(test_isServiceRunning_active)
{
    MockProcessRunner mock;
    mock.setResponse(
        "systemctl",
        QStringList() << "is-active" << "testservice",
        0,
        "active",
        ""
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(mgr.isServiceRunning());
}
END_TEST

START_TEST(test_isServiceRunning_inactive)
{
    MockProcessRunner mock;
    mock.setResponse(
        "systemctl",
        QStringList() << "is-active" << "testservice",
        0,
        "inactive",
        ""
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(!mgr.isServiceRunning());
}
END_TEST

START_TEST(test_startService_success)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "start" << "testservice",
        0,
        "",
        ""
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(mgr.startService());
}
END_TEST

START_TEST(test_startService_failure)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "start" << "testservice",
        1,
        "",
        "fail"
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(!mgr.startService());
}
END_TEST

START_TEST(test_stopService_success)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "stop" << "testservice",
        0,
        "",
        ""
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(mgr.stopService());
}
END_TEST

START_TEST(test_stopService_failure)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "stop" << "testservice",
        1,
        "",
        "fail"
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(!mgr.stopService());
}
END_TEST

START_TEST(test_enableService_success)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "enable" << "--now" << "testservice",
        0,
        "",
        ""
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(mgr.enableService());
}
END_TEST

START_TEST(test_enableService_failure)
{
    MockProcessRunner mock;
    mock.setResponse(
        "pkexec",
        QStringList() << "systemctl" << "enable" << "--now" << "testservice",
        1,
        "",
        "fail"
    );
    ServiceManager mgr("testservice", &mock);
    ck_assert(!mgr.enableService());
}
END_TEST

Suite* servicemanager_suite(void)
{
    Suite* s = suite_create("ServiceManager");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_isServiceRunning_active);
    tcase_add_test(tc, test_isServiceRunning_inactive);
    tcase_add_test(tc, test_startService_success);
    tcase_add_test(tc, test_startService_failure);
    tcase_add_test(tc, test_stopService_success);
    tcase_add_test(tc, test_stopService_failure);
    tcase_add_test(tc, test_enableService_success);
    tcase_add_test(tc, test_enableService_failure);

    suite_add_tcase(s, tc);
    return s;
}

/* No main here: suite will be registered from the global test runner. */
