#include "calibration/PhCalibrationRecord.h"
#include <cstring>

namespace aquamax {
namespace {
constexpr uint32_t schema = 1;

uint32_t checksum(const PhCalibrationRecord& record) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < phCalibrationRecordWords - 1; ++index) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash = (hash ^ ((record[index] >> shift) & 0xffu)) * 16777619u;
        }
    }
    return hash;
}
}

bool encodePhCalibration(const PhCalibration& calibration, PhCalibrationRecord& record) {
    static_assert(sizeof(float) == sizeof(uint32_t), "Unsupported float representation");
    float health = 0.0f;
    if (!phSlopeHealth(calibration, health)) return false;
    record[0] = schema;
    std::memcpy(&record[1], &calibration.voltsPerPh, sizeof(float));
    std::memcpy(&record[2], &calibration.interceptVolts, sizeof(float));
    std::memcpy(&record[3], &calibration.baselineVoltsPerPh, sizeof(float));
    record[4] = checksum(record);
    return true;
}

bool decodePhCalibration(const PhCalibrationRecord& record, PhCalibration& calibration) {
    if (record[0] != schema || record[4] != checksum(record)) return false;
    PhCalibration candidate;
    candidate.valid = true;
    std::memcpy(&candidate.voltsPerPh, &record[1], sizeof(float));
    std::memcpy(&candidate.interceptVolts, &record[2], sizeof(float));
    std::memcpy(&candidate.baselineVoltsPerPh, &record[3], sizeof(float));
    float health = 0.0f;
    if (!phSlopeHealth(candidate, health)) return false;
    calibration = candidate;
    return true;
}

} // namespace aquamax