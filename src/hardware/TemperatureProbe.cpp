#include "hardware/TemperatureProbe.h"
#include "sensors/TemperatureDecode.h"

namespace aquamax {
namespace {
constexpr uint32_t conversionMs = 750;
constexpr uint32_t scanIntervalMs = 5000;
}

TemperatureProbe::TemperatureProbe(uint8_t pin) : bus_(pin) {}

bool TemperatureProbe::findProbe() {
    bus_.reset_search();
    uint8_t candidate[8];
    while (bus_.search(candidate)) {
        if (candidate[0] == 0x28 && oneWireCrc8(candidate, 7) == candidate[7]) {
            for (unsigned index = 0; index < 8; ++index) address_[index] = candidate[index];
            return true;
        }
    }
    return false;
}

bool TemperatureProbe::beginConversion() {
    if (!bus_.reset()) return false;
    bus_.select(address_);
    bus_.write(0xbe);
    uint8_t scratchpad[9];
    for (uint8_t& byte : scratchpad) byte = bus_.read();
    if (oneWireCrc8(scratchpad, 8) != scratchpad[8]) return false;
    if ((scratchpad[4] & 0x60u) != 0x60u) {
        if (!bus_.reset()) return false;
        bus_.select(address_);
        bus_.write(0x4e);
        bus_.write(scratchpad[2]);
        bus_.write(scratchpad[3]);
        bus_.write(0x7f);
    }
    if (!bus_.reset()) return false;
    bus_.select(address_);
    bus_.write(0x44, 0);
    return true;
}

bool TemperatureProbe::readConversion(float& value) {
    if (!bus_.reset()) return false;
    bus_.select(address_);
    bus_.write(0xbe);
    uint8_t scratchpad[9];
    for (uint8_t& byte : scratchpad) byte = bus_.read();
    return decodeTemperature(scratchpad, value);
}

TemperatureSample TemperatureProbe::update(uint32_t nowMs) {
    if (!found_ && (!scanned_ || nowMs - lastScanMs_ >= scanIntervalMs)) {
        scanned_ = true;
        lastScanMs_ = nowMs;
        found_ = findProbe();
        if (!found_) sample_.state = state_.update(ProbeState::disconnected);
    }
    if (!found_) {
        sample_.hasValue = false;
        return sample_;
    }
    if (!converting_) {
        if (beginConversion()) {
            converting_ = true;
            startedMs_ = nowMs;
        } else {
            found_ = false;
            sample_.hasValue = false;
            sample_.state = state_.update(ProbeState::disconnected);
        }
        return sample_;
    }
    if (nowMs - startedMs_ < conversionMs) return sample_;
    converting_ = false;
    float value = 0.0f;
    if (!readConversion(value)) {
        found_ = false;
        sample_.hasValue = false;
        sample_.state = state_.update(ProbeState::disconnected);
        return sample_;
    }
    sample_.state = state_.update(ProbeState::connected);
    sample_.hasValue = sample_.state == ProbeState::connected;
    sample_.valueC = value;
    sample_.lastSeenMs = nowMs;
    return sample_;
}

} // namespace aquamax