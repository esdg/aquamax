#include <unity.h>
#include "sensors/ClockStatus.h"

void setUp() {}
void tearDown() {}

void test_unsynchronized_clock_is_not_a_timestamp() {
    TEST_ASSERT_FALSE(aquamax::clockStatus(0, false).synchronized);
    TEST_ASSERT_FALSE(aquamax::clockStatus(1760000000, false).synchronized);
    TEST_ASSERT_FALSE(aquamax::clockStatus(0, true).synchronized);
    TEST_ASSERT_FALSE(aquamax::clockStatus(4294967296LL, true).synchronized);
}

void test_network_time_is_reported() {
    const auto status = aquamax::clockStatus(1760000000, true);
    TEST_ASSERT_TRUE(status.synchronized);
    TEST_ASSERT_EQUAL_UINT32(1760000000, status.unixSeconds);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_unsynchronized_clock_is_not_a_timestamp);
    RUN_TEST(test_network_time_is_reported);
    return UNITY_END();
}