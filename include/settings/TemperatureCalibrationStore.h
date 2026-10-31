#pragma once

#include <cstdint>

namespace aquamax {

struct TemperatureCalibration {
    float offsetC = 0.0f;
    uint32_t timestamp = 0;
    bool calibrated = false;
};

enum class TemperatureCalibrationLoad : uint8_t { defaults, loaded, error };

bool validTemperatureCalibration(const TemperatureCalibration& calibration);

class TemperatureCalibrationStore {
public:
    TemperatureCalibrationLoad load(TemperatureCalibration& calibration);
    bool save(const TemperatureCalibration& calibration);
};

} // namespace aquamax