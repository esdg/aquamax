#pragma once

#include <cstddef>
#include <cstdint>
#include "config/ph.h"

namespace aquamax {

struct PhVoltageLimits {
    float dividerRatio = phDefaults::dividerRatio;
    float adcSupplyVolts = phDefaults::adcSupplyVolts;
    float boardMinimumVolts = phDefaults::boardMinimumVolts;
    float boardMaximumVolts = phDefaults::boardMaximumVolts;
    float railMarginVolts = phDefaults::railMarginVolts;
};

enum class PhVoltageStatus : uint8_t { valid, railed, outOfRange, invalidConfiguration };

struct PhPoint {
    float ph;
    float boardVolts;
};

struct PhCalibration {
    bool valid = false;
    float voltsPerPh = 0.0f;
    float interceptVolts = 0.0f;
    float baselineVoltsPerPh = 0.0f;
};

PhVoltageStatus classifyPhVoltage(float adcVolts, const PhVoltageLimits& limits, float& boardVolts);
bool fitPhCalibration(const PhPoint* points, std::size_t count, const PhVoltageLimits& limits,
                      const PhCalibration& previous, PhCalibration& result);
bool phFromVoltage(float adcVolts, const PhVoltageLimits& limits, const PhCalibration& calibration, float& ph);
bool phSlopeHealth(const PhCalibration& calibration, float& percent);

class PhCalibrationSession {
public:
    void start();
    void cancel();
    bool capture(float ph, float boardVolts, const PhVoltageLimits& limits);
    bool fit(const PhVoltageLimits& limits, const PhCalibration& previous, PhCalibration& result) const;
    bool active() const { return active_; }
    std::size_t count() const { return count_; }
    const PhPoint* points() const { return points_; }

private:
    PhPoint points_[3] = {};
    std::size_t count_ = 0;
    bool active_ = false;
};

} // namespace aquamax