#pragma once

#include <cstddef>
#include <cstdint>
#include "sensors/HistoryFormat.h"

namespace aquamax {

enum class HistoryMetric : uint8_t { temperature, ph, tds };
enum class HistoryRead : uint8_t { found, missing, error };

class HistoryStorage {
public:
    virtual ~HistoryStorage() = default;
    virtual bool beginBatch() { return true; }
    virtual bool endBatch() { return true; }
    virtual HistoryRead read(HistoryMetric metric, std::size_t tier, uint32_t slot, uint8_t* bytes) = 0;
    virtual bool write(HistoryMetric metric, std::size_t tier, uint32_t slot, const uint8_t* bytes) = 0;
    virtual bool clear() = 0;
};

class HistoryEngine {
public:
    HistoryEngine(HistoryStorage& storage, HistoryMetric metric, float scale);
    bool sample(uint32_t timestamp, bool hasValue, float value);
    bool flush();
    HistoryRead read(std::size_t tier, uint32_t timestamp, HistoryRecord& record);
    void reset();
    bool healthy() const { return healthy_; }

private:
    struct Pending {
        HistoryRecord record;
        uint8_t tier;
    };
    bool write(std::size_t tier, const HistoryRecord& record);

    HistoryStorage& storage_;
    HistoryMetric metric_;
    HistoryBucket buckets_[historyTierCount];
    bool initialized_[historyTierCount] = {};
    static constexpr std::size_t pendingCapacity = 64;
    Pending pending_[pendingCapacity] = {};
    std::size_t pendingCount_ = 0;
    bool healthy_ = true;
};

} // namespace aquamax