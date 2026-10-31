#include <unity.h>
#include "sensors/HistoryFormat.h"

void setUp() {}
void tearDown() {}

void test_record_roundtrip_and_ring_wrap() {
    const auto tier = aquamax::historyTiers[0];
    const aquamax::HistoryRecord input{1704067200, -250, 2100, 2200, 6};
    uint8_t bytes[aquamax::historyRecordBytes] = {};
    TEST_ASSERT_TRUE(aquamax::encodeHistoryRecord(tier, input, bytes));
    aquamax::HistoryRecord output;
    const auto slot = aquamax::historySlot(tier, input.timestamp);
    TEST_ASSERT_TRUE(aquamax::decodeHistoryRecord(tier, slot, bytes, output));
    TEST_ASSERT_EQUAL_UINT32(input.timestamp, output.timestamp);
    TEST_ASSERT_EQUAL_INT16(input.minimum, output.minimum);
    TEST_ASSERT_EQUAL_INT16(input.average, output.average);
    TEST_ASSERT_EQUAL_INT16(input.maximum, output.maximum);
    TEST_ASSERT_EQUAL_UINT16(input.count, output.count);
    TEST_ASSERT_EQUAL_UINT32(slot, aquamax::historySlot(tier, input.timestamp + tier.intervalSeconds * tier.capacity));
    TEST_ASSERT_FALSE(aquamax::decodeHistoryRecord(tier, (slot + 1) % tier.capacity, bytes, output));
    bytes[6] ^= 1;
    TEST_ASSERT_FALSE(aquamax::decodeHistoryRecord(tier, slot, bytes, output));
}

void test_invalid_and_empty_records_are_rejected() {
    const auto tier = aquamax::historyTiers[1];
    uint8_t bytes[aquamax::historyRecordBytes] = {};
    aquamax::HistoryRecord record;
    TEST_ASSERT_FALSE(aquamax::decodeHistoryRecord(tier, 0, bytes, record));
    TEST_ASSERT_FALSE(aquamax::encodeHistoryRecord(tier, {1704067200, 100, 50, 200, 1}, bytes));
    TEST_ASSERT_FALSE(aquamax::encodeHistoryRecord(tier, {1704067201, 0, 1, 2, 1}, bytes));
    TEST_ASSERT_FALSE(aquamax::encodeHistoryRecord(tier, {1704067200, 0, 1, 2, 0}, bytes));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_record_roundtrip_and_ring_wrap);
    RUN_TEST(test_invalid_and_empty_records_are_rejected);
    return UNITY_END();
}