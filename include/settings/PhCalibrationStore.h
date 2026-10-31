#pragma once

#include "calibration/PhCalibration.h"

namespace aquamax {

enum class PhCalibrationLoad { loaded, notFound, error };

class PhCalibrationStore {
public:
    PhCalibrationLoad load(PhCalibration& calibration);
    bool commit(const PhPoint* points, std::size_t count, const PhVoltageLimits& limits,
                PhCalibration& calibration);
};

} // namespace aquamax