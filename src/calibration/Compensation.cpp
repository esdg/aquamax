#include "calibration/Compensation.h"
#include <cmath>

namespace aquamax {
namespace {
bool validTemperature(float value) { return std::isfinite(value) && value >= -5.0f && value <= 60.0f; }
}

bool resolveCompensationTemperature(bool fresh, float measuredC, float defaultC, CompensationTemperature& result) {
    if (!validTemperature(defaultC)) return false;
    if (fresh && !validTemperature(measuredC)) return false;
    result.valueC = fresh ? measuredC : defaultC;
    result.usedDefault = !fresh;
    return true;
}

bool temperatureCompensatedPh(float adcVolts, const PhVoltageLimits& limits, const PhCalibration& calibration,
                              float calibrationTemperatureC, float waterTemperatureC, float& ph) {
    float boardVolts = 0.0f;
    float health = 0.0f;
    if (!validTemperature(calibrationTemperatureC) || !validTemperature(waterTemperatureC) ||
        !phSlopeHealth(calibration, health) || classifyPhVoltage(adcVolts, limits, boardVolts) != PhVoltageStatus::valid) return false;
    const float neutralVolts = calibration.interceptVolts + 7.0f * calibration.voltsPerPh;
    const float slope = calibration.voltsPerPh * (waterTemperatureC + 273.15f) / (calibrationTemperatureC + 273.15f);
    const float value = 7.0f + (boardVolts - neutralVolts) / slope;
    if (!std::isfinite(value)) return false;
    ph = value < 0.0f ? 0.0f : value > 14.0f ? 14.0f : value;
    return true;
}

bool temperatureCompensatedTds(float conductivityMicrosiemens, float waterTemperatureC, float factor, float& ppm) {
    if (!std::isfinite(conductivityMicrosiemens) || conductivityMicrosiemens < 0.0f ||
        !validTemperature(waterTemperatureC) || !std::isfinite(factor) || factor <= 0.0f || factor > 2.0f) return false;
    const float value = conductivityMicrosiemens / (1.0f + 0.02f * (waterTemperatureC - 25.0f)) * factor;
    if (!std::isfinite(value)) return false;
    ppm = value;
    return true;
}

} // namespace aquamax