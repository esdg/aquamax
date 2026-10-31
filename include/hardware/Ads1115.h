#pragma once

#include <Wire.h>
#include <cstdint>

namespace aquamax {

enum class AdcResult : uint8_t { idle, pending, ready, error };

class Ads1115 {
public:
    explicit Ads1115(TwoWire& wire, uint8_t address = 0x48);
    bool start(uint8_t channel, uint32_t nowMs);
    AdcResult poll(uint32_t nowMs, int16_t& raw);
    void cancel();

private:
    bool writeRegister(uint8_t reg, uint16_t value);
    bool readRegister(uint8_t reg, uint16_t& value);
    TwoWire& wire_;
    uint8_t address_;
    bool pending_ = false;
    uint32_t startedMs_ = 0;
};

} // namespace aquamax