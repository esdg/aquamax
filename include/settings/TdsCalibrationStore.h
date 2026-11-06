#pragma once

#include <cstdint>

namespace aquamax {

struct TdsCalibration {
    float factor = 1.0f;
    uint32_t timestamp = 0;
    bool calibrated = false;
};

enum class TdsCalibrationLoad : uint8_t { defaults, loaded, error };

bool validTdsCalibration(const TdsCalibration& calibration);

class TdsCalibrationStore {
public:
    TdsCalibrationLoad load(TdsCalibration& calibration);
    bool save(const TdsCalibration& calibration);
};

} // namespace aquamax