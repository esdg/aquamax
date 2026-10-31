#include "sensors/HistoryAggregate.h"
#include <cmath>

namespace aquamax {

void HistoryAggregate::add(const HistoryRecord& record) {
    if (!record.count || record.minimum > record.average || record.average > record.maximum) return;
    if (!count_) { minimum_ = record.minimum; maximum_ = record.maximum; }
    else {
        if (record.minimum < minimum_) minimum_ = record.minimum;
        if (record.maximum > maximum_) maximum_ = record.maximum;
    }
    total_ += static_cast<int64_t>(record.average) * record.count;
    count_ += record.count;
}

bool HistoryAggregate::result(HistoryPoint& point) const {
    if (!count_) return false;
    point = {minimum_, static_cast<int16_t>(std::lround(static_cast<double>(total_) / count_)), maximum_, count_};
    return true;
}

} // namespace aquamax