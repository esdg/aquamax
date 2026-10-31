#include <unity.h>
#include <limits>
#include "sensors/SignalFilter.h"

void setUp() {}
void tearDown() {}

void test_filter_rejects_spike_and_nonfinite() {
    aquamax::SignalFilter filter;
    float result = -1.0f;
    for (int index = 0; index < 4; ++index) TEST_ASSERT_FALSE(filter.add(1.0f, result));
    TEST_ASSERT_FALSE(filter.add(std::numeric_limits<float>::quiet_NaN(), result));
    TEST_ASSERT_TRUE(filter.add(100.0f, result));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result);
    TEST_ASSERT_TRUE(filter.add(1.0f, result));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, result);
}

void test_reset_discards_old_samples() {
    aquamax::SignalFilter filter;
    float result = 0.0f;
    for (int index = 0; index < 5; ++index) filter.add(1.0f, result);
    filter.reset();
    for (int index = 0; index < 4; ++index) TEST_ASSERT_FALSE(filter.add(3.0f, result));
    TEST_ASSERT_TRUE(filter.add(3.0f, result));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, result);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_filter_rejects_spike_and_nonfinite);
    RUN_TEST(test_reset_discards_old_samples);
    return UNITY_END();
}