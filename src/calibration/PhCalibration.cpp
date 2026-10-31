#include "calibration/PhCalibration.h"
#include <cmath>

namespace aquamax {
namespace {
constexpr float minimumPhSpan = 0.01f;
constexpr float minimumSlope = 0.000001f;

bool validCalibration(const PhCalibration& calibration) {
    return calibration.valid && std::isfinite(calibration.voltsPerPh) &&
           std::isfinite(calibration.interceptVolts) && std::isfinite(calibration.baselineVoltsPerPh) &&
           calibration.voltsPerPh < -minimumSlope && calibration.baselineVoltsPerPh < -minimumSlope;
}
}

PhVoltageStatus classifyPhVoltage(float adcVolts, const PhVoltageLimits& limits, float& boardVolts) {
    if (!std::isfinite(limits.dividerRatio) || limits.dividerRatio <= 0.0f || limits.dividerRatio > 1.0f ||
        !std::isfinite(limits.adcSupplyVolts) || limits.adcSupplyVolts <= 0.0f ||
        !std::isfinite(limits.boardMinimumVolts) || !std::isfinite(limits.boardMaximumVolts) ||
        limits.boardMinimumVolts < 0.0f || limits.boardMaximumVolts <= limits.boardMinimumVolts ||
        !std::isfinite(limits.railMarginVolts) || limits.railMarginVolts <= 0.0f ||
        2.0f * limits.railMarginVolts >= limits.boardMaximumVolts - limits.boardMinimumVolts ||
        2.0f * limits.railMarginVolts >= limits.adcSupplyVolts) return PhVoltageStatus::invalidConfiguration;
    if (!std::isfinite(adcVolts) || adcVolts < 0.0f || adcVolts > limits.adcSupplyVolts) return PhVoltageStatus::outOfRange;
    const float measuredBoardVolts = adcVolts / limits.dividerRatio;
    if (!std::isfinite(measuredBoardVolts) || measuredBoardVolts < limits.boardMinimumVolts ||
        measuredBoardVolts > limits.boardMaximumVolts) return PhVoltageStatus::outOfRange;
    if (adcVolts <= limits.railMarginVolts || adcVolts >= limits.adcSupplyVolts - limits.railMarginVolts ||
        measuredBoardVolts <= limits.boardMinimumVolts + limits.railMarginVolts ||
        measuredBoardVolts >= limits.boardMaximumVolts - limits.railMarginVolts) return PhVoltageStatus::railed;
    boardVolts = measuredBoardVolts;
    return PhVoltageStatus::valid;
}

bool fitPhCalibration(const PhPoint* points, std::size_t count, const PhVoltageLimits& limits,
                      const PhCalibration& previous, PhCalibration& result) {
    if (points == nullptr || (count != 2 && count != 3) || (previous.valid && !validCalibration(previous))) return false;
    float meanPh = 0.0f;
    float meanVolts = 0.0f;
    float minimumPh = 14.0f;
    float maximumPh = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        const PhPoint& point = points[index];
        float boardVolts = 0.0f;
        if (!std::isfinite(point.ph) || point.ph < 0.0f || point.ph > 14.0f ||
            classifyPhVoltage(point.boardVolts * limits.dividerRatio, limits, boardVolts) != PhVoltageStatus::valid) return false;
        meanPh += point.ph;
        meanVolts += point.boardVolts;
        if (point.ph < minimumPh) minimumPh = point.ph;
        if (point.ph > maximumPh) maximumPh = point.ph;
    }
    if (maximumPh - minimumPh < minimumPhSpan) return false;
    meanPh /= static_cast<float>(count);
    meanVolts /= static_cast<float>(count);
    float variance = 0.0f;
    float covariance = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        const float delta = points[index].ph - meanPh;
        variance += delta * delta;
        covariance += delta * (points[index].boardVolts - meanVolts);
    }
    const float slope = covariance / variance;
    const float intercept = meanVolts - slope * meanPh;
    if (!std::isfinite(slope) || slope >= -minimumSlope || !std::isfinite(intercept)) return false;
    PhCalibration candidate;
    candidate.valid = true;
    candidate.voltsPerPh = slope;
    candidate.interceptVolts = intercept;
    candidate.baselineVoltsPerPh = previous.valid ? previous.baselineVoltsPerPh : slope;
    result = candidate;
    return true;
}

bool phFromVoltage(float adcVolts, const PhVoltageLimits& limits, const PhCalibration& calibration, float& ph) {
    float boardVolts = 0.0f;
    if (!validCalibration(calibration) || classifyPhVoltage(adcVolts, limits, boardVolts) != PhVoltageStatus::valid) return false;
    const float measuredPh = (boardVolts - calibration.interceptVolts) / calibration.voltsPerPh;
    if (!std::isfinite(measuredPh) || measuredPh < 0.0f || measuredPh > 14.0f) return false;
    ph = measuredPh;
    return true;
}

bool phSlopeHealth(const PhCalibration& calibration, float& percent) {
    if (!validCalibration(calibration)) return false;
    const float health = 100.0f * calibration.voltsPerPh / calibration.baselineVoltsPerPh;
    if (!std::isfinite(health)) return false;
    percent = health;
    return true;
}

} // namespace aquamax