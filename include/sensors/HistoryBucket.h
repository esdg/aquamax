#pragma once

#include <cstdint>

namespace aquamax {

struct HistoryRecord {
    uint32_t timestamp = 0;
    int16_t minimum = 0;
    int16_t average = 0;
    int16_t maximum = 0;
    uint16_t count = 0;
};

class HistoryBucket {
public:
    HistoryBucket(uint32_t intervalSeconds, float scale);
    bool advance(uint32_t timestamp, bool hasValue, float value, HistoryRecord& completed);
    bool current(HistoryRecord& record) const;
    bool restore(const HistoryRecord& record);
    void reset();

private:
    uint32_t intervalSeconds_;
    float scale_;
    uint32_t bucketStart_ = 0;
    int64_t total_ = 0;
    int16_t minimum_ = 0;
    int16_t maximum_ = 0;
    uint16_t count_ = 0;
    bool active_ = false;
};

} // namespace aquamax