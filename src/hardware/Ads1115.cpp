#include "hardware/Ads1115.h"
#include "sensors/AdcMath.h"

namespace aquamax {
namespace {
constexpr uint32_t conversionWaitMs = 9;
constexpr uint32_t conversionTimeoutMs = 100;
constexpr uint8_t conversionRegister = 0;
constexpr uint8_t configurationRegister = 1;
}

Ads1115::Ads1115(TwoWire& wire, uint8_t address) : wire_(wire), address_(address) {}

bool Ads1115::writeRegister(uint8_t reg, uint16_t value) {
    wire_.beginTransmission(address_);
    wire_.write(reg);
    wire_.write(static_cast<uint8_t>(value >> 8));
    wire_.write(static_cast<uint8_t>(value));
    return wire_.endTransmission() == 0;
}

bool Ads1115::readRegister(uint8_t reg, uint16_t& value) {
    wire_.beginTransmission(address_);
    wire_.write(reg);
    if (wire_.endTransmission(false) != 0) return false;
    if (wire_.requestFrom(address_, static_cast<uint8_t>(2)) != 2) return false;
    const uint8_t high = wire_.read();
    const uint8_t low = wire_.read();
    value = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
    return true;
}

bool Ads1115::start(uint8_t channel, uint32_t nowMs) {
    uint16_t config = 0;
    if (pending_ || !adsSingleShotConfig(channel, config) || !writeRegister(configurationRegister, config)) return false;
    pending_ = true;
    startedMs_ = nowMs;
    return true;
}

AdcResult Ads1115::poll(uint32_t nowMs, int16_t& raw) {
    if (!pending_) return AdcResult::idle;
    const uint32_t elapsed = nowMs - startedMs_;
    if (elapsed < conversionWaitMs) return AdcResult::pending;
    if (elapsed >= conversionTimeoutMs) {
        pending_ = false;
        return AdcResult::error;
    }
    uint16_t config = 0;
    if (!readRegister(configurationRegister, config)) {
        pending_ = false;
        return AdcResult::error;
    }
    if ((config & 0x8000u) == 0) return AdcResult::pending;
    uint16_t value = 0;
    pending_ = false;
    if (!readRegister(conversionRegister, value)) return AdcResult::error;
    const int32_t signedValue = value & 0x8000u ? static_cast<int32_t>(value) - 65536 : value;
    raw = static_cast<int16_t>(signedValue);
    return AdcResult::ready;
}

void Ads1115::cancel() {
    pending_ = false;
}

} // namespace aquamax