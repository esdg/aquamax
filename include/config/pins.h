#pragma once

#include <cstdint>

namespace aquamax::pins {

constexpr int tftSclk = 12;
constexpr int tftMosi = 11;
constexpr int tftDc = 13;
constexpr int tftReset = 14;
constexpr int tftTemperatureCs = 17;
constexpr int tftPhCs = 16;
constexpr int tftTdsCs = 15;
constexpr int i2cSda = 8;
constexpr int i2cScl = 9;
constexpr int oneWire = 10;
constexpr int tdsEnable = 21;
constexpr bool tdsEnableActiveHigh = true;
constexpr uint32_t tdsWarmupMs = 1000;
constexpr int cableDetect = 5;
constexpr int backlight = 18;

constexpr int assigned[] = {
    tftSclk, tftMosi, tftDc, tftReset, tftTemperatureCs, tftPhCs,
    tftTdsCs, i2cSda, i2cScl, oneWire, tdsEnable, cableDetect, backlight,
};

constexpr bool validAssignments() {
    for (unsigned index = 0; index < sizeof(assigned) / sizeof(assigned[0]); ++index) {
        const int pin = assigned[index];
        if (pin < 0 || pin > 48 || pin == 0 || pin == 3 || pin == 19 || pin == 20 ||
            (pin >= 26 && pin <= 38) || pin == 45 || pin == 46 || pin == 48) {
            return false;
        }
        for (unsigned other = index + 1; other < sizeof(assigned) / sizeof(assigned[0]); ++other) {
            if (pin == assigned[other]) return false;
        }
    }
    return true;
}

static_assert(validAssignments(), "Invalid or conflicting AquaMax pin assignments");

} // namespace aquamax::pins