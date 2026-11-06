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

bool tdsEstimate(float voltage, float waterTemperatureC, float calibrationFactor, float& ppm) {
    if (!std::isfinite(voltage) || voltage < 0.0f || voltage > 2.3f ||
        !std::isfinite(calibrationFactor) || calibrationFactor < 0.25f || calibrationFactor > 4.0f) return false;
    const float conductivity = voltage * (857.39f + voltage * (-255.86f + voltage * 133.42f));
    return temperatureCompensatedTds(conductivity, waterTemperatureC, calibrationFactor * 0.5f, ppm);
}

bool tdsCalibrationFactor(float voltage, float waterTemperatureC, float referencePpm, float& factor) {
    float estimate = 0.0f;
    if (!std::isfinite(referencePpm) || referencePpm <= 0.0f || referencePpm > 1000.0f ||
        !tdsEstimate(voltage, waterTemperatureC, 1.0f, estimate) || estimate < 5.0f) return false;
    const float candidate = referencePpm / estimate;
    if (!std::isfinite(candidate) || candidate < 0.25f || candidate > 4.0f) return false;
    factor = candidate;
    return true;
}

} // namespace aquamax