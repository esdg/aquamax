#pragma once

#include <cstdint>

namespace aquamax {

bool adsSingleShotConfig(uint8_t channel, uint16_t& config);
float adsRawToVolts(int16_t raw);

} // namespace aquamax