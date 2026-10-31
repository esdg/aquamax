#include <unity.h>
#include <array>
#include <map>
#include <tuple>
#include "sensors/HistoryEngine.h"

class FakeHistoryStorage : public aquamax::HistoryStorage {
public:
    using Key = std::tuple<int, std::size_t, uint32_t>;
    std::map<Key, std::array<uint8_t, aquamax::historyRecordBytes>> records;
    bool fail = false;

    aquamax::HistoryRead read(aquamax::HistoryMetric metric, std::size_t tier, uint32_t slot, uint8_t* bytes) override {
        if (fail) return aquamax::HistoryRead::error;
        const auto found = records.find({static_cast<int>(metric), tier, slot});
        if (found == records.end()) return aquamax::HistoryRead::missing;
        for (std::size_t index = 0; index < found->second.size(); ++index) bytes[index] = found->second[index];
        return aquamax::HistoryRead::found;
    }
    bool write(aquamax::HistoryMetric metric, std::size_t tier, uint32_t slot, const uint8_t* bytes) override {
        if (fail) return false;
        auto& destination = records[{static_cast<int>(metric), tier, slot}];
        for (std::size_t index = 0; index < destination.size(); ++index) destination[index] = bytes[index];
        return true;
    }
    bool clear() override { if (fail) return false; records.clear(); return true; }
};

void setUp() {}
void tearDown() {}

void test_buffer_flush_and_restore_current_bucket() {
    FakeHistoryStorage storage;
    constexpr uint32_t start = 1704067200;
    aquamax::HistoryEngine engine(storage, aquamax::HistoryMetric::temperature, 100.0f);
    TEST_ASSERT_FALSE(engine.sample(0, true, 20.0f));
    TEST_ASSERT_TRUE(engine.sample(start, true, 20.0f));
    TEST_ASSERT_TRUE(engine.sample(start + 10, true, 22.0f));
    aquamax::HistoryRecord record;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::HistoryRead::found), static_cast<int>(engine.read(0, start, record)));
    TEST_ASSERT_EQUAL_INT16(2000, record.average);
    TEST_ASSERT_TRUE(engine.flush());
    aquamax::HistoryEngine recovered(storage, aquamax::HistoryMetric::temperature, 100.0f);
    TEST_ASSERT_TRUE(recovered.sample(start + 20, true, 24.0f));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::HistoryRead::found), static_cast<int>(recovered.read(1, start, record)));
    TEST_ASSERT_EQUAL_UINT16(3, record.count);
    TEST_ASSERT_EQUAL_INT16(2200, record.average);
    TEST_ASSERT_EQUAL_INT16(2000, record.minimum);
    TEST_ASSERT_EQUAL_INT16(2400, record.maximum);
}

void test_missing_values_and_storage_failure_are_explicit() {
    FakeHistoryStorage storage;
    constexpr uint32_t start = 1704067200;
    aquamax::HistoryEngine engine(storage, aquamax::HistoryMetric::ph, 100.0f);
    TEST_ASSERT_TRUE(engine.sample(start, false, 7.0f));
    TEST_ASSERT_TRUE(engine.flush());
    TEST_ASSERT_TRUE(storage.records.empty());
    TEST_ASSERT_TRUE(engine.sample(start + 10, true, 7.0f));
    storage.fail = true;
    TEST_ASSERT_FALSE(engine.flush());
    TEST_ASSERT_FALSE(engine.healthy());
    storage.fail = false;
    TEST_ASSERT_TRUE(engine.flush());
    TEST_ASSERT_TRUE(engine.healthy());
    aquamax::HistoryRecord record;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::HistoryRead::found), static_cast<int>(engine.read(0, start + 10, record)));
    TEST_ASSERT_EQUAL_INT16(700, record.average);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_flush_and_restore_current_bucket);
    RUN_TEST(test_missing_values_and_storage_failure_are_explicit);
    return UNITY_END();
}