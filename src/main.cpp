#include <Arduino.h>
#include "config/pins.h"
#if defined(AQUAMAX_WEB_PREVIEW)
#include "webserver/DashboardPreview.h"
#elif defined(AQUAMAX_TEMPERATURE_PROBE)
#include "hardware/TemperatureProbe.h"
#elif defined(AQUAMAX_ADC_PROBE)
#include "hardware/Ads1115.h"
#include "sensors/AdcMath.h"
#else
#include "sensors/Simulation.h"
#endif
#include "settings/SettingsStore.h"

namespace {

#if defined(AQUAMAX_WEB_PREVIEW)
aquamax::DashboardPreview dashboard;
#elif !defined(AQUAMAX_TEMPERATURE_PROBE) && !defined(AQUAMAX_ADC_PROBE)
aquamax::Simulation simulation;
#endif
uint32_t lastSample = 0;

#if defined(AQUAMAX_TEMPERATURE_PROBE) && !defined(AQUAMAX_WEB_PREVIEW)
const char* probeStateName(aquamax::ProbeState state) {
    switch (state) {
        case aquamax::ProbeState::connected: return "connected";
        case aquamax::ProbeState::disconnected: return "disconnected";
        case aquamax::ProbeState::unstable: return "unstable";
        case aquamax::ProbeState::out_of_range: return "out_of_range";
        case aquamax::ProbeState::disabled: return "disabled";
    }
    return "unknown";
}
#elif !defined(AQUAMAX_ADC_PROBE) && !defined(AQUAMAX_WEB_PREVIEW)
void printReading(const char* label, const aquamax::SimulatedReading& reading) {
    Serial.print(label);
    Serial.print('=');
    if (reading.hasValue) Serial.print(reading.value, 2);
    else Serial.print("--");
    Serial.print(" state=");
    Serial.print(static_cast<unsigned>(reading.state));
    Serial.print(' ');
}
#endif

} // namespace

void setup() {
    Serial.begin(115200);
#if defined(AQUAMAX_WEB_PREVIEW)
    dashboard.begin();
#elif defined(AQUAMAX_TEMPERATURE_PROBE)
    Serial.println("AquaMax DS18B20 test: GPIO 10, pH and TDS not sampled");
#elif defined(AQUAMAX_ADC_PROBE)
    Wire.begin(aquamax::pins::i2cSda, aquamax::pins::i2cScl);
    Serial.println("AquaMax ADS1115 test: address 0x48, A0 raw voltage only");
#else
    Serial.println("AquaMax simulation: no physical sensors or outputs are enabled");
#endif
    aquamax::Settings settings;
    aquamax::SettingsStore store;
    const aquamax::SettingsLoad settingsStatus = store.load(settings);
    Serial.println(settingsStatus == aquamax::SettingsLoad::loaded ? "Settings: loaded" :
                   settingsStatus == aquamax::SettingsLoad::defaults ? "Settings: defaults" : "Settings: error; using defaults");
#if !defined(AQUAMAX_TEMPERATURE_PROBE) && !defined(AQUAMAX_ADC_PROBE) && !defined(AQUAMAX_WEB_PREVIEW)
    Serial.println("Commands: 0 all absent, 1 all present, t temperature absent, p pH absent, d TDS absent");
#endif
}

void loop() {
#if defined(AQUAMAX_WEB_PREVIEW)
    dashboard.update(millis());
#elif defined(AQUAMAX_TEMPERATURE_PROBE)
    static aquamax::TemperatureProbe temperature(aquamax::pins::oneWire);
    const uint32_t now = millis();
    const aquamax::TemperatureSample sample = temperature.update(now);
    if (now - lastSample >= 1000) {
        lastSample = now;
        Serial.print("DS18B20 temp_C=");
        if (sample.hasValue) Serial.print(sample.valueC, 2);
        else Serial.print("--");
        Serial.print(" state=");
        Serial.println(probeStateName(sample.state));
    }
#elif defined(AQUAMAX_ADC_PROBE)
    static aquamax::Ads1115 adc(Wire);
    static bool pending = false;
    const uint32_t now = millis();
    if (pending) {
        int16_t raw = 0;
        const aquamax::AdcResult result = adc.poll(now, raw);
        if (result == aquamax::AdcResult::ready) {
            pending = false;
            Serial.print("ADS1115 A0 raw=");
            Serial.print(raw);
            Serial.print(" volts=");
            Serial.println(aquamax::adsRawToVolts(raw), 4);
        } else if (result == aquamax::AdcResult::error) {
            pending = false;
            Serial.println("ADS1115 A0 error: read failed or conversion timed out");
        }
    } else if (now - lastSample >= 1000) {
        lastSample = now;
        pending = adc.start(0, now);
        if (!pending) Serial.println("ADS1115 A0 error: start failed");
    }
#else
    while (Serial.available()) {
        switch (Serial.read()) {
            case '0': simulation.setPresent(false, false, false); break;
            case '1': simulation.setPresent(true, true, true); break;
            case 't': simulation.setPresent(false, true, true); break;
            case 'p': simulation.setPresent(true, false, true); break;
            case 'd': simulation.setPresent(true, true, false); break;
            default: break;
        }
    }
    const uint32_t now = millis();
    if (now - lastSample >= 1000) {
        lastSample = now;
        const aquamax::SimulatedSnapshot snapshot = simulation.sample(now);
        Serial.print("SIM ");
        printReading("temp_C", snapshot.temperature);
        printReading("pH", snapshot.ph);
        printReading("tds_ppm", snapshot.tds);
        Serial.println(snapshot.defaultCompensation ? "compensation=default" : "compensation=measured");
    }
#endif
}