#pragma once

#include <cstddef>
#include <cstdint>

namespace aquamax {

uint8_t oneWireCrc8(const uint8_t* bytes, std::size_t length);
bool decodeTemperature(const uint8_t (&scratchpad)[9], float& temperatureC);

} // namespace aquamax