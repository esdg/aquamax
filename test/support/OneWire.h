#pragma once

#include <cstdint>
#include <cstring>
#include "sensors/TemperatureDecode.h"

class OneWire {
public:
    inline static bool present = true;
    inline static uint8_t scratchpad[9] = {};

    explicit OneWire(uint8_t) {}

    static void resetDevice() {
        present = true;
        const uint8_t initial[9] = {0x90, 0x01, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0};
        std::memcpy(scratchpad, initial, sizeof(scratchpad));
        scratchpad[8] = aquamax::oneWireCrc8(scratchpad, 8);
    }

    void reset_search() { searched_ = false; }
    bool search(uint8_t* address) {
        if (!present || searched_) return false;
        searched_ = true;
        const uint8_t rom[8] = {0x28, 1, 2, 3, 4, 5, 6, 0};
        std::memcpy(address, rom, sizeof(rom));
        address[7] = aquamax::oneWireCrc8(address, 7);
        return true;
    }
    uint8_t reset() { return present ? 1 : 0; }
    void select(const uint8_t*) {}
    void write(uint8_t value, uint8_t = 0) {
        if (value == 0xbe) readIndex_ = 0;
    }
    uint8_t read() { return readIndex_ < 9 ? scratchpad[readIndex_++] : 0xff; }

private:
    bool searched_ = false;
    uint8_t readIndex_ = 0;
};