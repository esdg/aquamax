#pragma once

#if defined(AQUAMAX_TFT)
#include <cstdint>
#include "sensors/SensorSnapshot.h"

namespace aquamax {

bool beginTftGauges();
void updateTftGauges(const SensorSnapshot& snapshot, bool temperatureAlert, uint32_t nowMs);

}
#endif