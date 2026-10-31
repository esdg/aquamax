#include "settings/TemperatureCalibrationStore.h"
#include <Preferences.h>
#include <cmath>
#include <cstring>

namespace aquamax {
namespace {
constexpr uint32_t schema = 1;
constexpr std::size_t recordWords = 5;

uint32_t checksum(const uint32_t* words) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < recordWords - 1; ++index) {
        for (unsigned shift = 0; shift < 32; shift += 8) hash = (hash ^ ((words[index] >> shift) & 0xffu)) * 16777619u;
    }
    return hash;
}
}

bool validTemperatureCalibration(const TemperatureCalibration& calibration) {
    return std::isfinite(calibration.offsetC) && calibration.offsetC >= -5.0f && calibration.offsetC <= 5.0f &&
           (calibration.calibrated || (calibration.offsetC == 0.0f && calibration.timestamp == 0));
}

TemperatureCalibrationLoad TemperatureCalibrationStore::load(TemperatureCalibration& calibration) {
    calibration = {};
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return TemperatureCalibrationLoad::error;
    if (!preferences.isKey("tempcal")) {
        preferences.end();
        return TemperatureCalibrationLoad::defaults;
    }
    uint32_t words[recordWords] = {};
    const std::size_t size = preferences.getBytes("tempcal", words, sizeof(words));
    preferences.end();
    if (size != sizeof(words) || words[0] != schema || words[3] > 1 || words[4] != checksum(words)) return TemperatureCalibrationLoad::error;
    TemperatureCalibration candidate;
    std::memcpy(&candidate.offsetC, &words[1], sizeof(float));
    candidate.timestamp = words[2];
    candidate.calibrated = words[3] != 0;
    if (!validTemperatureCalibration(candidate)) return TemperatureCalibrationLoad::error;
    calibration = candidate;
    return TemperatureCalibrationLoad::loaded;
}

bool TemperatureCalibrationStore::save(const TemperatureCalibration& calibration) {
    if (!validTemperatureCalibration(calibration)) return false;
    uint32_t words[recordWords] = {schema, 0, calibration.timestamp, calibration.calibrated ? 1u : 0u, 0};
    static_assert(sizeof(float) == sizeof(uint32_t), "Unsupported float representation");
    std::memcpy(&words[1], &calibration.offsetC, sizeof(float));
    words[4] = checksum(words);
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return false;
    const bool saved = preferences.putBytes("tempcal", words, sizeof(words)) == sizeof(words);
    preferences.end();
    return saved;
}

} // namespace aquamax