#pragma once

#if defined(AQUAMAX_TFT)
#include <cstdint>
#include "sensors/SensorSnapshot.h"

namespace aquamax {

struct TftSetupInfo {
	bool active = false;
	bool failed = false;
	char ssid[24] = {};
	char password[17] = {};
};

bool beginTftGauges();
void showTftSplash(uint32_t elapsedMs);
void showTftSetup(const TftSetupInfo& setup);
void updateTftGauges(const SensorSnapshot& snapshot, bool temperatureAlert, uint32_t nowMs);

}
#endif