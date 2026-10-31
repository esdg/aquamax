#include "sensors/AdcMath.h"

namespace aquamax {

bool adsSingleShotConfig(uint8_t channel, uint16_t& config) {
    if (channel > 3) return false;
    config = static_cast<uint16_t>(0x8000u | ((4u + channel) << 12) | 0x0200u | 0x0100u | 0x0080u | 0x0003u);
    return true;
}

float adsRawToVolts(int16_t raw) {
    return static_cast<float>(raw) * 0.000125f;
}

} // namespace aquamax