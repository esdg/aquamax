#pragma once

#include "calibration/PhCalibration.h"

namespace aquamax {

struct CompensationTemperature {
    float valueC = 25.0f;
    bool usedDefault = true;
};

bool resolveCompensationTemperature(bool fresh, float measuredC, float defaultC, CompensationTemperature& result);
bool temperatureCompensatedPh(float adcVolts, const PhVoltageLimits& limits, const PhCalibration& calibration,
                              float calibrationTemperatureC, float waterTemperatureC, float& ph);
bool temperatureCompensatedTds(float conductivityMicrosiemens, float waterTemperatureC, float factor, float& ppm);
bool tdsEstimate(float voltage, float waterTemperatureC, float calibrationFactor, float& ppm);
bool tdsCalibrationFactor(float voltage, float waterTemperatureC, float referencePpm, float& factor);

} // namespace aquamax