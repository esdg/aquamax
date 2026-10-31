#include "sensors/TemperatureDecode.h"

namespace aquamax {

uint8_t oneWireCrc8(const uint8_t* bytes, std::size_t length) {
    uint8_t crc = 0;
    for (std::size_t index = 0; index < length; ++index) {
        uint8_t value = bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint8_t mix = (crc ^ value) & 1u;
            crc >>= 1;
            if (mix) crc ^= 0x8cu;
            value >>= 1;
        }
    }
    return crc;
}

bool decodeTemperature(const uint8_t (&scratchpad)[9], float& temperatureC) {
    if (oneWireCrc8(scratchpad, 8) != scratchpad[8]) return false;
    const uint16_t bits = static_cast<uint16_t>(scratchpad[0]) |
                          (static_cast<uint16_t>(scratchpad[1]) << 8);
    const int32_t raw = bits & 0x8000u ? static_cast<int32_t>(bits) - 65536 : bits;
    const float value = static_cast<float>(raw) / 16.0f;
    if (value == 85.0f || value == -127.0f || value < -55.0f || value > 125.0f) return false;
    temperatureC = value;
    return true;
}

} // namespace aquamax