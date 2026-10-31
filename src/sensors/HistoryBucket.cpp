#include "sensors/HistoryBucket.h"
#include <cmath>
#include <limits>

namespace aquamax {

HistoryBucket::HistoryBucket(uint32_t intervalSeconds, float scale)
    : intervalSeconds_(intervalSeconds), scale_(scale) {}

bool HistoryBucket::advance(uint32_t timestamp, bool hasValue, float value, HistoryRecord& completed) {
    if (intervalSeconds_ == 0 || !std::isfinite(scale_) || scale_ <= 0.0f) return false;
    const uint32_t start = timestamp - timestamp % intervalSeconds_;
    bool finished = false;
    if (active_ && start != bucketStart_) {
        finished = current(completed);
        reset();
    }
    const double scaled = static_cast<double>(value) * scale_;
    if (!hasValue || !std::isfinite(scaled) ||
        scaled < std::numeric_limits<int16_t>::min() || scaled > std::numeric_limits<int16_t>::max() ||
        count_ == std::numeric_limits<uint16_t>::max()) return finished;
    const int16_t sample = static_cast<int16_t>(std::lround(scaled));
    if (!active_) {
        active_ = true;
        bucketStart_ = start;
        minimum_ = sample;
        maximum_ = sample;
    } else {
        if (sample < minimum_) minimum_ = sample;
        if (sample > maximum_) maximum_ = sample;
    }
    total_ += sample;
    ++count_;
    return finished;
}

bool HistoryBucket::current(HistoryRecord& record) const {
    if (!active_ || count_ == 0) return false;
    record = {bucketStart_, minimum_, static_cast<int16_t>(std::lround(static_cast<double>(total_) / count_)), maximum_, count_};
    return true;
}

bool HistoryBucket::restore(const HistoryRecord& record) {
    if (!intervalSeconds_ || !record.timestamp || record.timestamp % intervalSeconds_ || !record.count ||
        record.minimum > record.average || record.average > record.maximum) return false;
    bucketStart_ = record.timestamp;
    minimum_ = record.minimum;
    maximum_ = record.maximum;
    total_ = static_cast<int64_t>(record.average) * record.count;
    count_ = record.count;
    active_ = true;
    return true;
}

void HistoryBucket::reset() {
    bucketStart_ = 0;
    total_ = 0;
    minimum_ = 0;
    maximum_ = 0;
    count_ = 0;
    active_ = false;
}

} // namespace aquamax