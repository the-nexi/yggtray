#include <check.h>

// Forward declarations from other test files
extern Suite* servicemanager_suite(void);
extern Suite* peermanager_suite(void);

int main(void)
{
    int number_failed = 0;
    SRunner* sr = srunner_create(servicemanager_suite());
    srunner_add_suite(sr, peermanager_suite());

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? 0 : 1;
}
