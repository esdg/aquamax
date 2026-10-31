#include <unity.h>
#include <limits>
#include "sensors/HistoryBucket.h"

void setUp() {}
void tearDown() {}

void test_bucket_aggregates_scaled_samples() {
    aquamax::HistoryBucket bucket(60, 100.0f);
    aquamax::HistoryRecord record;
    TEST_ASSERT_FALSE(bucket.advance(120, true, 20.25f, record));
    TEST_ASSERT_FALSE(bucket.advance(130, true, 22.00f, record));
    TEST_ASSERT_FALSE(bucket.advance(140, true, 21.00f, record));
    TEST_ASSERT_TRUE(bucket.current(record));
    TEST_ASSERT_EQUAL_UINT32(120, record.timestamp);
    TEST_ASSERT_EQUAL_INT16(2025, record.minimum);
    TEST_ASSERT_EQUAL_INT16(2108, record.average);
    TEST_ASSERT_EQUAL_INT16(2200, record.maximum);
    TEST_ASSERT_EQUAL_UINT16(3, record.count);
    TEST_ASSERT_TRUE(bucket.advance(180, true, -2.0f, record));
    TEST_ASSERT_EQUAL_UINT32(120, record.timestamp);
    TEST_ASSERT_TRUE(bucket.current(record));
    TEST_ASSERT_EQUAL_INT16(-200, record.average);
}

void test_missing_and_invalid_values_never_create_records() {
    aquamax::HistoryBucket bucket(10, 100.0f);
    aquamax::HistoryRecord record;
    TEST_ASSERT_FALSE(bucket.advance(100, false, 25.0f, record));
    TEST_ASSERT_FALSE(bucket.advance(100, true, std::numeric_limits<float>::quiet_NaN(), record));
    TEST_ASSERT_FALSE(bucket.advance(100, true, 500.0f, record));
    TEST_ASSERT_FALSE(bucket.current(record));
    TEST_ASSERT_FALSE(bucket.advance(100, true, 24.0f, record));
    TEST_ASSERT_TRUE(bucket.advance(110, false, 24.0f, record));
    TEST_ASSERT_EQUAL_UINT16(1, record.count);
    TEST_ASSERT_FALSE(bucket.current(record));
}

void test_clock_changes_do_not_mix_buckets() {
    aquamax::HistoryBucket bucket(60, 100.0f);
    aquamax::HistoryRecord record;
    bucket.advance(240, true, 20.0f, record);
    TEST_ASSERT_TRUE(bucket.advance(120, true, 30.0f, record));
    TEST_ASSERT_EQUAL_UINT32(240, record.timestamp);
    TEST_ASSERT_EQUAL_INT16(2000, record.average);
    TEST_ASSERT_TRUE(bucket.current(record));
    TEST_ASSERT_EQUAL_UINT32(120, record.timestamp);
    TEST_ASSERT_EQUAL_INT16(3000, record.average);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bucket_aggregates_scaled_samples);
    RUN_TEST(test_missing_and_invalid_values_never_create_records);
    RUN_TEST(test_clock_changes_do_not_mix_buckets);
    return UNITY_END();
}