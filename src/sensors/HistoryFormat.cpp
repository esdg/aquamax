#include "sensors/HistoryFormat.h"

namespace aquamax {

const HistoryTier historyTiers[historyTierCount] = {
    {10, 8640}, {60, 10080}, {300, 8640}, {3600, 8760},
};

namespace {
void put16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t get16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1] << 8);
}

void put32(uint8_t* destination, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) destination[index] = static_cast<uint8_t>(value >> (index * 8));
}

uint32_t get32(const uint8_t* bytes) {
    uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) value |= static_cast<uint32_t>(bytes[index]) << (index * 8);
    return value;
}

uint32_t checksum(const uint8_t* bytes) {
    uint32_t hash = 2166136261u ^ 0x414d4831u;
    for (unsigned index = 0; index < 12; ++index) hash = (hash ^ bytes[index]) * 16777619u;
    return hash;
}

bool valid(const HistoryTier& tier, const HistoryRecord& record) {
    return tier.intervalSeconds && tier.capacity && record.timestamp &&
           record.timestamp % tier.intervalSeconds == 0 && record.count &&
           record.minimum <= record.average && record.average <= record.maximum;
}
}

uint32_t historySlot(const HistoryTier& tier, uint32_t timestamp) {
    return tier.intervalSeconds && tier.capacity ? (timestamp / tier.intervalSeconds) % tier.capacity : 0;
}

bool encodeHistoryRecord(const HistoryTier& tier, const HistoryRecord& record, uint8_t* bytes) {
    if (!bytes || !valid(tier, record)) return false;
    put32(bytes, record.timestamp);
    put16(bytes + 4, static_cast<uint16_t>(record.minimum));
    put16(bytes + 6, static_cast<uint16_t>(record.average));
    put16(bytes + 8, static_cast<uint16_t>(record.maximum));
    put16(bytes + 10, record.count);
    put32(bytes + 12, checksum(bytes));
    return true;
}

bool decodeHistoryRecord(const HistoryTier& tier, uint32_t slot, const uint8_t* bytes, HistoryRecord& record) {
    if (!bytes || slot >= tier.capacity || get32(bytes + 12) != checksum(bytes)) return false;
    HistoryRecord candidate{get32(bytes), static_cast<int16_t>(get16(bytes + 4)),
                            static_cast<int16_t>(get16(bytes + 6)), static_cast<int16_t>(get16(bytes + 8)),
                            get16(bytes + 10)};
    if (!valid(tier, candidate) || historySlot(tier, candidate.timestamp) != slot) return false;
    record = candidate;
    return true;
}

} // namespace aquamax