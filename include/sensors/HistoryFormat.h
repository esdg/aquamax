#pragma once

#include <cstddef>
#include <cstdint>
#include "sensors/HistoryBucket.h"

namespace aquamax {

struct HistoryTier {
    uint32_t intervalSeconds;
    uint32_t capacity;
};

constexpr std::size_t historyTierCount = 4;
constexpr std::size_t historyRecordBytes = 16;
extern const HistoryTier historyTiers[historyTierCount];

uint32_t historySlot(const HistoryTier& tier, uint32_t timestamp);
bool encodeHistoryRecord(const HistoryTier& tier, const HistoryRecord& record, uint8_t* bytes);
bool decodeHistoryRecord(const HistoryTier& tier, uint32_t slot, const uint8_t* bytes, HistoryRecord& record);

} // namespace aquamax