#pragma once

#include <cstddef>
#include <cstdint>
#include "calibration/PhCalibration.h"

namespace aquamax {

constexpr std::size_t phCalibrationRecordWords = 5;
using PhCalibrationRecord = uint32_t[phCalibrationRecordWords];

bool encodePhCalibration(const PhCalibration& calibration, PhCalibrationRecord& record);
bool decodePhCalibration(const PhCalibrationRecord& record, PhCalibration& calibration);

} // namespace aquamax