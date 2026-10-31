#include "settings/Settings.h"
#include "sensors/MeasurementScheduler.h"
#include <cmath>

namespace aquamax {

bool validSettings(const Settings& settings) {
    const MeasurementScheduler scheduler({settings.tdsIntervalMs, settings.tdsWarmupMs,
                                          settings.tdsSampleMs, settings.phRecoveryMs});
    return scheduler.valid() && settings.tdsIntervalMs >= 5000 && settings.tdsIntervalMs <= 3600000 &&
           std::isfinite(settings.defaultTemperatureC) && settings.defaultTemperatureC >= 0.0f &&
           settings.defaultTemperatureC <= 50.0f;
}

} // namespace aquamax