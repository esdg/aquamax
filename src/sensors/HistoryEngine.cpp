#include "sensors/HistoryEngine.h"

namespace aquamax {

HistoryEngine::HistoryEngine(HistoryStorage& storage, HistoryMetric metric, float scale)
    : storage_(storage), metric_(metric),
      buckets_{HistoryBucket(10, scale), HistoryBucket(60, scale),
               HistoryBucket(300, scale), HistoryBucket(3600, scale)} {}

bool HistoryEngine::sample(uint32_t timestamp, bool hasValue, float value) {
    if (!timestamp) return false;
    if (pendingCount_ > pendingCapacity - historyTierCount && !flush()) return false;
    for (std::size_t tier = 0; tier < historyTierCount; ++tier) {
        if (!initialized_[tier]) {
            const uint32_t start = timestamp - timestamp % historyTiers[tier].intervalSeconds;
            HistoryRecord previous;
            const HistoryRead status = read(tier, start, previous);
            if (status == HistoryRead::error) { healthy_ = false; return false; }
            if (status == HistoryRead::found) buckets_[tier].restore(previous);
            initialized_[tier] = true;
        }
    }
    for (std::size_t tier = 0; tier < historyTierCount; ++tier) {
        HistoryRecord completed;
        if (buckets_[tier].advance(timestamp, hasValue, value, completed)) {
            pending_[pendingCount_++] = {completed, static_cast<uint8_t>(tier)};
        }
    }
    return true;
}

bool HistoryEngine::write(std::size_t tier, const HistoryRecord& record) {
    uint8_t bytes[historyRecordBytes] = {};
    return encodeHistoryRecord(historyTiers[tier], record, bytes) &&
           storage_.write(metric_, tier, historySlot(historyTiers[tier], record.timestamp), bytes);
}

bool HistoryEngine::flush() {
    if (!storage_.beginBatch()) { healthy_ = false; return false; }
    bool success = true;
    for (std::size_t tier = 0; tier < historyTierCount; ++tier) {
        for (std::size_t index = 0; index < pendingCount_; ++index) {
            if (pending_[index].tier == tier && !write(tier, pending_[index].record)) { success = false; break; }
        }
        if (!success) break;
        HistoryRecord current;
        if (buckets_[tier].current(current) && !write(tier, current)) { success = false; break; }
    }
    if (!storage_.endBatch()) success = false;
    if (success) pendingCount_ = 0;
    healthy_ = success;
    return success;
}

HistoryRead HistoryEngine::read(std::size_t tier, uint32_t timestamp, HistoryRecord& record) {
    if (tier >= historyTierCount || timestamp % historyTiers[tier].intervalSeconds) return HistoryRead::missing;
    HistoryRecord current;
    if (buckets_[tier].current(current) && current.timestamp == timestamp) { record = current; return HistoryRead::found; }
    for (std::size_t index = pendingCount_; index > 0; --index) {
        const Pending& pending = pending_[index - 1];
        if (pending.tier == tier && pending.record.timestamp == timestamp) { record = pending.record; return HistoryRead::found; }
    }
    uint8_t bytes[historyRecordBytes] = {};
    const uint32_t slot = historySlot(historyTiers[tier], timestamp);
    const HistoryRead status = storage_.read(metric_, tier, slot, bytes);
    if (status != HistoryRead::found) return status;
    if (!decodeHistoryRecord(historyTiers[tier], slot, bytes, record) || record.timestamp != timestamp) return HistoryRead::missing;
    return HistoryRead::found;
}

void HistoryEngine::reset() {
    for (std::size_t tier = 0; tier < historyTierCount; ++tier) {
        buckets_[tier].reset();
        initialized_[tier] = false;
    }
    pendingCount_ = 0;
    healthy_ = true;
}

} // namespace aquamax