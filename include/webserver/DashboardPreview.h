#pragma once

#if defined(AQUAMAX_WEB_PREVIEW)
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <atomic>
#include <cstdio>
#include "config/pins.h"
#include "hardware/TemperatureProbe.h"
#include "sensors/SensorSnapshot.h"
#include "sensors/TimeZone.h"
#include "settings/TemperatureCalibrationStore.h"
#include "webserver/HistoryFileStorage.h"

namespace aquamax {

class DashboardPreview {
public:
    void begin();
    void update(uint32_t now);

private:
    void startSetup();
    void serveSetup();
    void serveSetupStatus();
    void connectNetwork();
    void setTimezone();
    void serveHistory(bool csv);
    void serveHistoryStatus();
    void clearHistory();
    void serveCalibration();
    void changeTemperatureCalibration();
    void uploadAsset();
    void updateHistory(uint32_t now);
    void serveState();
    String stateJson();
    void updateNetwork(uint32_t now);
    static void networkTask(void* context);
    void serveAsset(const char* path, const char* contentType);
    bool onSetupNetwork();

    WebServer server_{80};
    WebSocketsServer socket_{81};
    DNSServer dns_;
    TemperatureProbe temperature_{pins::oneWire};
    SensorSnapshotBuilder sensorBuilder_;
    SensorSnapshot snapshot_;
    TemperatureCalibrationStore temperatureCalibrationStore_;
    TemperatureCalibration temperatureCalibration_;
    std::atomic<float> temperatureOffset_{0.0f};
    bool temperatureCalibrationError_ = false;
    bool calibrationWritten_ = false;
    uint32_t lastCalibrationWriteMs_ = 0;
    HistoryFileStorage historyStorage_;
    HistoryEngine temperatureHistory_{historyStorage_, HistoryMetric::temperature, 100.0f};
    HistoryEngine phHistory_{historyStorage_, HistoryMetric::ph, 100.0f};
    HistoryEngine tdsHistory_{historyStorage_, HistoryMetric::tds, 1.0f};
    const TimeZone* timezone_ = nullptr;
    portMUX_TYPE snapshotMutex_ = portMUX_INITIALIZER_UNLOCKED;
    String setupToken_;
    String pendingSsid_;
    String pendingPassword_;
    uint32_t attemptStarted_ = 0;
    uint32_t connectedSince_ = 0;
    uint32_t lastSubmit_ = 0;
    uint32_t connectRequested_ = 0;
    uint32_t lastSensorReportMs_ = 0;
    uint32_t lastBroadcastMs_ = 0;
    uint32_t lastHistorySampleMs_ = 0;
    uint32_t lastHistoryFlushMs_ = 0;
    bool historyStarted_ = false;
    std::FILE* assetUpload_ = nullptr;
    String assetUploadName_;
    std::size_t assetUploadBytes_ = 0;
    bool assetUploadSucceeded_ = false;
    std::atomic<int> disconnectReason_{0};
    int lastDisconnectReason_ = 0;
    bool submitted_ = false;
    bool connectQueued_ = false;
    bool attempting_ = false;
    bool setupActive_ = false;
    bool connected_ = false;
    bool setupFailed_ = false;
    bool filesystemReady_ = false;
};

} // namespace aquamax
#endif