#pragma once

#include <cstdint>
#include "sensors/HistoryBucket.h"

namespace aquamax {

struct HistoryPoint {
    int16_t minimum = 0;
    int16_t average = 0;
    int16_t maximum = 0;
    uint32_t count = 0;
};

class HistoryAggregate {
public:
    void add(const HistoryRecord& record);
    bool result(HistoryPoint& point) const;

private:
    int64_t total_ = 0;
    int16_t minimum_ = 0;
    int16_t maximum_ = 0;
    uint32_t count_ = 0;
};

} // namespace aquamax