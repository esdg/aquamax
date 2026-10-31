#pragma once

#include <cstdint>
#include <vector>

class TwoWire {
public:
    bool present = true;
    bool shortRead = false;
    uint16_t configuration = 0;
    uint16_t conversion = 0;
    uint8_t address = 0;
    uint8_t selectedRegister = 0;
    std::vector<uint8_t> transmission;
    unsigned readOffset = 0;

    void beginTransmission(uint8_t target) {
        address = target;
        transmission.clear();
    }

    void write(uint8_t value) { transmission.push_back(value); }

    uint8_t endTransmission(bool = true) {
        if (!present) return 4;
        if (transmission.empty()) return 4;
        selectedRegister = transmission[0];
        if (transmission.size() == 3 && selectedRegister == 1) {
            configuration = static_cast<uint16_t>((transmission[1] << 8) | transmission[2]);
        }
        return 0;
    }

    uint8_t requestFrom(uint8_t, uint8_t) {
        readOffset = 0;
        return present && !shortRead ? 2 : 0;
    }

    uint8_t read() {
        const uint16_t value = selectedRegister == 1 ? configuration : conversion;
        return readOffset++ == 0 ? static_cast<uint8_t>(value >> 8) : static_cast<uint8_t>(value);
    }
};