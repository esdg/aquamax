#include <unity.h>
#include "sensors/TimeZone.h"

void setUp() {}
void tearDown() {}

void test_supported_time_zones_and_unknown_input() {
    const auto* paris = aquamax::findTimeZone("Europe/Paris");
    TEST_ASSERT_NOT_NULL(paris);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", paris->rule);
    TEST_ASSERT_NOT_NULL(aquamax::findTimeZone("Etc/UTC"));
    TEST_ASSERT_NOT_NULL(aquamax::findTimeZone("Australia/Sydney"));
    TEST_ASSERT_NULL(aquamax::findTimeZone("not/a/zone"));
    TEST_ASSERT_NULL(aquamax::findTimeZone(nullptr));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_supported_time_zones_and_unknown_input);
    return UNITY_END();
}