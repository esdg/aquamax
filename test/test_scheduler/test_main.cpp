#include <unity.h>
#include "sensors/MeasurementScheduler.h"

void setUp() {}
void tearDown() {}

void test_quiet_warm_sample_and_recover() {
    aquamax::MeasurementScheduler scheduler({10000, 1000, 250, 500});
    TEST_ASSERT_TRUE(scheduler.valid());
    TEST_ASSERT_TRUE(scheduler.update(0, true).samplePh);
    auto warm = scheduler.update(10000, true);
    TEST_ASSERT_TRUE(warm.tdsPower);
    TEST_ASSERT_FALSE(warm.samplePh);
    TEST_ASSERT_FALSE(warm.sampleTds);
    auto sample = scheduler.update(11000, true);
    TEST_ASSERT_TRUE(sample.tdsPower);
    TEST_ASSERT_TRUE(sample.sampleTds);
    auto recovery = scheduler.update(11250, true);
    TEST_ASSERT_FALSE(recovery.tdsPower);
    TEST_ASSERT_FALSE(recovery.samplePh);
    TEST_ASSERT_TRUE(scheduler.update(11750, true).samplePh);
}

void test_cancel_and_invalid_schedule_are_off() {
    aquamax::MeasurementScheduler scheduler({10000, 1000, 250, 500});
    scheduler.update(0, true);
    TEST_ASSERT_TRUE(scheduler.update(10000, true).tdsPower);
    auto cancelled = scheduler.update(10001, false);
    TEST_ASSERT_FALSE(cancelled.tdsPower);
    TEST_ASSERT_FALSE(cancelled.sampleTds);
    aquamax::MeasurementScheduler invalid({1000, 1000, 250, 500});
    TEST_ASSERT_FALSE(invalid.valid());
    TEST_ASSERT_FALSE(invalid.update(10000, true).tdsPower);
}

void test_clock_wrap() {
    aquamax::MeasurementScheduler scheduler({10000, 1000, 250, 500});
    scheduler.update(UINT32_MAX - 5000, true);
    TEST_ASSERT_TRUE(scheduler.update(5000, true).tdsPower);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_quiet_warm_sample_and_recover);
    RUN_TEST(test_cancel_and_invalid_schedule_are_off);
    RUN_TEST(test_clock_wrap);
    return UNITY_END();
}