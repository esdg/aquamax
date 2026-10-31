#include "settings/PhCalibrationStore.h"
#include "calibration/PhCalibrationRecord.h"
#include <Preferences.h>

namespace aquamax {

PhCalibrationLoad PhCalibrationStore::load(PhCalibration& calibration) {
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return PhCalibrationLoad::error;
    if (!preferences.isKey("phcal")) {
        preferences.end();
        calibration = PhCalibration{};
        return PhCalibrationLoad::notFound;
    }
    PhCalibrationRecord record = {};
    const std::size_t size = preferences.getBytes("phcal", record, sizeof(record));
    preferences.end();
    if (size != sizeof(record) || !decodePhCalibration(record, calibration)) return PhCalibrationLoad::error;
    return PhCalibrationLoad::loaded;
}

bool PhCalibrationStore::commit(const PhPoint* points, std::size_t count, const PhVoltageLimits& limits,
                                PhCalibration& calibration) {
    PhCalibration previous;
    if (load(previous) == PhCalibrationLoad::error) return false;
    PhCalibration candidate;
    if (!fitPhCalibration(points, count, limits, previous, candidate)) return false;
    PhCalibrationRecord record = {};
    if (!encodePhCalibration(candidate, record)) return false;
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return false;
    const bool saved = preferences.putBytes("phcal", record, sizeof(record)) == sizeof(record);
    preferences.end();
    if (!saved) return false;
    calibration = candidate;
    return true;
}

} // namespace aquamax