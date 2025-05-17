#include <check.h>
#include <stdio.h>

// Forward declarations from other test files
extern Suite* servicemanager_suite(void);
extern Suite* peermanager_suite(void);

int main(void)
{
    int number_failed = 0;
    SRunner* sr = srunner_create(servicemanager_suite());
    srunner_add_suite(sr, peermanager_suite());

    srunner_run_all(sr, CK_VERBOSE);
    number_failed = srunner_ntests_failed(sr);
    int total_checks = srunner_ntests_run(sr);
    srunner_free(sr);

    // Print summary again as last line
    if (number_failed == 0) {
        printf("100%%: Checks: %d, Failures: 0, Errors: 0\n", total_checks);
    } else {
        printf("FAILED: Checks: %d, Failures: %d\n", total_checks, number_failed);
    }

    return (number_failed == 0) ? 0 : 1;
}
