#if defined(AQUAMAX_WEB_PREVIEW)
#include "webserver/DashboardPreview.h"
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <stdlib.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <unistd.h>
#include "config/pins.h"
#include "sensors/ClockStatus.h"
#include "sensors/HistoryAggregate.h"
#if defined(AQUAMAX_PH_PROBE)
#include "sensors/AdcMath.h"
#endif
#if defined(AQUAMAX_TDS_PROBE)
#include "calibration/Compensation.h"
#endif
#include "display/TftGauge.h"
#include "webserver/SetupPage.h"

namespace aquamax {
namespace {
constexpr uint32_t connectionTimeoutMs = 20000;
constexpr uint32_t setupGraceMs = 60000;
constexpr uint32_t submitIntervalMs = 5000;
constexpr uint32_t responseGraceMs = 1000;
constexpr uint32_t broadcastIntervalMs = 1000;
constexpr uint32_t historySampleMs = 10000;
constexpr uint32_t historyFlushMs = 300000;
constexpr uint32_t calibrationWriteIntervalMs = 5000;
constexpr uint32_t alertWriteIntervalMs = 5000;
constexpr uint32_t journalWriteIntervalMs = 1000;
constexpr uint32_t systemWriteIntervalMs = 5000;
constexpr uint8_t monitorTemperature = 1;
constexpr uint8_t monitorPh = 2;
constexpr uint8_t monitorTds = 4;
std::atomic<bool> networkTimeReceived{false};

struct WifiCredentials {
    char ssid[33];
    char password[64];
};

class FallbackUri final : public Uri {
public:
    FallbackUri() : Uri("") {}
    Uri* clone() const override { return new FallbackUri(*this); }
    bool canHandle(const String&, std::vector<String>&) override { return true; }
};

const char* stateName(ProbeState state) {
    switch (state) {
        case ProbeState::connected: return "connected";
        case ProbeState::disconnected: return "disconnected";
        case ProbeState::unstable: return "unstable";
        case ProbeState::out_of_range: return "out_of_range";
        case ProbeState::disabled: return "disabled";
        case ProbeState::signal_fault: return "signal_fault";
        case ProbeState::waiting: return "waiting";
    }
    return "unknown";
}

const char* alertStateName(AlertState state) {
    switch (state) {
        case AlertState::normal: return "normal";
        case AlertState::low: return "low";
        case AlertState::high: return "high";
        case AlertState::probeFault: return "probe_fault";
    }
    return "normal";
}

const char* alertEventName(AlertEvent event) {
    switch (event) {
        case AlertEvent::none: return "none";
        case AlertEvent::low: return "low";
        case AlertEvent::high: return "high";
        case AlertEvent::probeFault: return "probe_fault";
        case AlertEvent::recovered: return "recovered";
    }
    return "none";
}

const char* probeWarningName(ProbeWarning warning) {
    switch (warning) {
        case ProbeWarning::normal: return "normal";
        case ProbeWarning::signalFault: return "signal_fault";
        case ProbeWarning::outOfRange: return "out_of_range";
        case ProbeWarning::unstable: return "unstable";
    }
    return "normal";
}

bool parseFloat(const String& text, float& value) {
    if (!text.length()) return false;
    char* end = nullptr;
    value = strtof(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value);
}

bool parseSeconds(const String& text, uint32_t maximumSeconds, uint32_t& milliseconds) {
    if (!text.length()) return false;
    for (unsigned index = 0; index < text.length(); ++index) if (text[index] < '0' || text[index] > '9') return false;
    char* end = nullptr;
    const unsigned long seconds = strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0' || seconds > maximumSeconds) return false;
    milliseconds = static_cast<uint32_t>(seconds) * 1000;
    return true;
}

String jsonText(const char* text) {
    String escaped;
    escaped += '"';
    for (const char* character = text; *character; ++character) {
        if (*character == '\n') { escaped += F("\\n"); continue; }
        if (*character == '\r') { escaped += F("\\r"); continue; }
        if (*character == '\t') { escaped += F("\\t"); continue; }
        if (*character == '"' || *character == '\\') escaped += '\\';
        escaped += *character;
    }
    escaped += '"';
    return escaped;
}

bool allowedOrigin(const String& origin) {
    return origin.equalsIgnoreCase("http://aquamax.local") ||
           origin == String("http://") + WiFi.localIP().toString() ||
           origin == String("http://") + WiFi.softAPIP().toString();
}
}

void DashboardPreview::begin() {
    Preferences monitoringStorage;
    if (monitoringStorage.begin("aquamax", false)) {
        const uint8_t mask = monitoringStorage.getUChar("monitor", 7);
        if (mask <= 7) monitoringMask_.store(mask);
        else monitoringStorageError_ = true;
        monitoringStorage.end();
    } else monitoringStorageError_ = true;
#if defined(AQUAMAX_TDS_PROBE)
    digitalWrite(pins::tdsEnable, LOW);
    pinMode(pins::tdsEnable, OUTPUT);
    esp_timer_create_args_t tdsTimerArgs{};
    tdsTimerArgs.callback = &DashboardPreview::stopTdsPower;
    tdsTimerArgs.arg = this;
    tdsTimerArgs.name = "tds-power";
    if (esp_timer_create(&tdsTimerArgs, &tdsPowerTimer_) != ESP_OK) tdsPowerTimer_ = nullptr;
    tdsCalibrationError_ = tdsCalibrationStore_.load(tdsCalibration_) == TdsCalibrationLoad::error;
    tdsScheduler_.update(millis() - 60000u, true);
#endif
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    digitalWrite(pins::tdsEnable, LOW);
    pinMode(pins::tdsEnable, OUTPUT);
    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = &DashboardPreview::stopTdsDiagnostic;
    timerArgs.arg = this;
    timerArgs.name = "tds-test";
    if (esp_timer_create(&timerArgs, &tdsDiagnosticTimer_) != ESP_OK) tdsDiagnosticTimer_ = nullptr;
#endif
#if defined(AQUAMAX_PH_PROBE)
    phWireReady_ = Wire.begin(pins::i2cSda, pins::i2cScl);
    if (phWireReady_) Wire.setTimeOut(25);
    else phAdcError_ = true;
    phCalibrationError_ = phCalibrationStore_.load(phCalibration_) == PhCalibrationLoad::error;
#endif
#if defined(AQUAMAX_TFT)
    tftRendererReady_ = beginTftGauges();
    if (tftRendererReady_) showTftSplash(0);
    else Serial.println("TFT initialization failed; dashboard remains available.");
#endif
    alertSettingsError_ = alertSettingsStore_.load(alertConfig_) == AlertSettingsLoad::error;
    temperatureCalibrationError_ = temperatureCalibrationStore_.load(temperatureCalibration_) == TemperatureCalibrationLoad::error;
    temperatureOffset_.store(temperatureCalibration_.offsetC);
    sntp_set_time_sync_notification_cb([](struct timeval*) { networkTimeReceived.store(true); });
    timezone_ = findTimeZone("Europe/Paris");
    Preferences clockStorage;
    if (clockStorage.begin("clock", true)) {
        const String savedZone = clockStorage.getString("timezone", "Europe/Paris");
        if (const TimeZone* saved = findTimeZone(savedZone.c_str())) timezone_ = saved;
        clockStorage.end();
    }
    setenv("TZ", timezone_->rule, 1);
    tzset();
    filesystemReady_ = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    journalReady_ = filesystemReady_ && eventJournal_.begin();
    if (!filesystemReady_) Serial.println("Dashboard filesystem unavailable; upload the dashboard filesystem.");
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([this](arduino_event_id_t, arduino_event_info_t info) {
        disconnectReason_.store(info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    server_.on("/", HTTP_GET, [this] { if (onSetupNetwork()) serveSetup(); else serveAsset("/index.html", "text/html"); });
    server_.on("/app.js", HTTP_GET, [this] { serveAsset("/app.js", "application/javascript"); });
    server_.on("/app.css", HTTP_GET, [this] { serveAsset("/app.css", "text/css"); });
    server_.on("/manifest.webmanifest", HTTP_GET, [this] { serveAsset("/manifest.webmanifest", "application/manifest+json"); });
    server_.on("/service-worker.js", HTTP_GET, [this] { serveAsset("/service-worker.js", "application/javascript"); });
    server_.on("/icon-192.png", HTTP_GET, [this] { serveAsset("/icon-192.png", "image/png"); });
    server_.on("/icon-512.png", HTTP_GET, [this] { serveAsset("/icon-512.png", "image/png"); });
    server_.on("/logo-aquamax.svg", HTTP_GET, [this] { serveAsset("/logo-aquamax.svg", "image/svg+xml"); });
    server_.on("/jumbo.jpg", HTTP_GET, [this] { serveAsset("/jumbo.jpg", "image/jpeg"); });
    server_.on("/api/state", HTTP_GET, [this] { serveState(); });
    server_.on("/api/timezone", HTTP_POST, [this] { setTimezone(); });
    server_.on("/api/monitoring", HTTP_GET, [this] { serveMonitoring(); });
    server_.on("/api/monitoring", HTTP_POST, [this] { changeMonitoring(); });
    server_.on("/api/history", HTTP_GET, [this] { serveHistory(false); });
    server_.on("/api/history/export.csv", HTTP_GET, [this] { serveHistory(true); });
    server_.on("/api/history/status", HTTP_GET, [this] { serveHistoryStatus(); });
    server_.on("/api/history", HTTP_DELETE, [this] { clearHistory(); });
    server_.on("/api/calibration", HTTP_GET, [this] { serveCalibration(); });
    server_.on("/api/calibration/temp", HTTP_POST, [this] { changeTemperatureCalibration(); });
#if defined(AQUAMAX_PH_PROBE)
    server_.on("/api/calibration/ph", HTTP_POST, [this] { changePhCalibration(); });
#endif
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    server_.on("/api/diagnostic/tds", HTTP_POST, [this] { changeTdsDiagnostic(); });
#endif
#if defined(AQUAMAX_TDS_PROBE)
    server_.on("/api/calibration/tds", HTTP_POST, [this] { changeTdsCalibration(); });
#endif
    server_.on("/api/alerts", HTTP_GET, [this] { serveAlerts(); });
    server_.on("/api/alerts", HTTP_POST, [this] { changeAlerts(); });
    server_.on("/api/events", HTTP_GET, [this] { serveEvents(); });
    server_.on("/api/events", HTTP_POST, [this] { addEvent(); });
    server_.on("/api/events", HTTP_DELETE, [this] { deleteEvent(); });
    server_.on("/api/tips", HTTP_GET, [this] { serveTips(); });
    server_.on("/api/system", HTTP_GET, [this] { serveSystem(); });
    server_.on("/api/system/config", HTTP_GET, [this] { serveConfiguration(); });
    server_.on("/api/system/config", HTTP_POST, [this] { restoreConfiguration(); });
    server_.on("/api/system/reboot", HTTP_POST, [this] { reboot(); });
    server_.on("/api/system/ota", HTTP_POST, [this] {
        if (!allowedOrigin(server_.header("Origin"))) server_.send(403, "text/plain", "Request rejected");
        else if (!firmwareUploadSucceeded_) server_.send(400, "text/plain", "Firmware update failed");
        else {
            server_.send(200, "application/json", "{\"ok\":true}");
            rebootPending_ = true;
            rebootRequestedMs_ = millis();
        }
        firmwareUploadSucceeded_ = false;
    }, [this] { uploadFirmware(); });
    server_.on("/api/dashboard-assets", HTTP_POST, [this] {
        if (!allowedOrigin(server_.header("Origin"))) server_.send(403, "text/plain", "Request rejected");
        else server_.send(assetUploadSucceeded_ ? 200 : 400, "application/json", assetUploadSucceeded_ ? "{\"ok\":true}" : "{\"ok\":false}");
        assetUploadSucceeded_ = false;
    }, [this] { uploadAsset(); });
    server_.on("/setup", HTTP_GET, [this] { serveSetup(); });
    server_.on("/setup", HTTP_POST, [this] { connectNetwork(); });
    server_.on("/setup/networks", HTTP_GET, [this] { serveSetupNetworks(); });
    server_.on("/setup/status", HTTP_GET, [this] { serveSetupStatus(); });
    server_.on(FallbackUri(), HTTP_ANY, [this] {
        if (onSetupNetwork()) {
            server_.sendHeader("Location", "http://192.168.4.1/setup");
            server_.send(302, "text/plain", "Open Wi-Fi setup");
        } else {
            server_.send(404, "text/plain", "Not found");
        }
    });
    static const char* headers[] = {"Origin"};
    server_.collectHeaders(headers, 1);
    server_.begin();
    static const char* mandatoryHeaders[] = {"Origin"};
    socket_.onValidateHttpHeader([](String name, String value) {
        if (!name.equalsIgnoreCase("Origin")) return true;
        return allowedOrigin(value);
    }, mandatoryHeaders, 1);
    socket_.onEvent([this](uint8_t client, WStype_t type, uint8_t* payload, size_t) {
        if (type == WStype_CONNECTED) {
            if (String(reinterpret_cast<const char*>(payload)) != "/ws") socket_.disconnect(client);
            else {
                String state = stateJson();
                socket_.sendTXT(client, state);
            }
        } else if (type == WStype_TEXT || type == WStype_BIN) {
            socket_.disconnect(client);
        }
    });
    socket_.begin();
    socket_.enableHeartbeat(15000, 3000, 2);

    Preferences storage;
    WifiCredentials credentials{};
    bool saved = false;
    if (storage.begin("wifi-preview", true)) {
        saved = storage.getBytesLength("network") == sizeof(credentials) &&
                storage.getBytes("network", &credentials, sizeof(credentials)) == sizeof(credentials);
        storage.end();
    }
    credentials.ssid[sizeof(credentials.ssid) - 1] = '\0';
    credentials.password[sizeof(credentials.password) - 1] = '\0';
    if (saved && credentials.ssid[0] != '\0') {
        Serial.println("Connecting to saved Wi-Fi network...");
        WiFi.begin(credentials.ssid, credentials.password);
        attempting_ = true;
        attemptStarted_ = millis();
    } else {
        startSetup();
    }
    if (xTaskCreatePinnedToCore(networkTask, "aquamax-network", 8192, this, 1, nullptr, 0) != pdPASS) {
        Serial.println("Unable to start network task; dashboard unavailable.");
    }
#if defined(AQUAMAX_TFT)
    startupStartedMs_ = millis();
    startup_.begin(startupStartedMs_, saved && credentials.ssid[0] != '\0');
#endif
}

void DashboardPreview::startSetup() {
    if (setupActive_) return;
    char name[24];
    char password[17];
    char token[33];
    snprintf(name, sizeof(name), "AquaMax-Setup-%04X", static_cast<unsigned>(ESP.getEfuseMac() & 0xffffu));
    snprintf(password, sizeof(password), "%08X%08X", static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
    snprintf(token, sizeof(token), "%08X%08X%08X%08X", static_cast<unsigned>(esp_random()),
             static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
    setupToken_ = token;
    if (!WiFi.softAP(name, password)) {
#if defined(AQUAMAX_TFT)
        portENTER_CRITICAL(&setupDisplayMutex_);
        setupDisplay_ = {};
        setupDisplay_.failed = true;
        portEXIT_CRITICAL(&setupDisplayMutex_);
#endif
        Serial.println("Unable to start Wi-Fi setup access point.");
        return;
    }
#if defined(AQUAMAX_TFT)
    TftSetupInfo setup;
    setup.active = true;
    std::snprintf(setup.ssid, sizeof(setup.ssid), "%s", name);
    std::snprintf(setup.password, sizeof(setup.password), "%s", password);
    portENTER_CRITICAL(&setupDisplayMutex_);
    setupDisplay_ = setup;
    portEXIT_CRITICAL(&setupDisplayMutex_);
#endif
    setupActive_ = true;
    dns_.start(53, "*", WiFi.softAPIP());
    if (!mdnsStarted_) mdnsStarted_ = MDNS.begin("aquamax");
    Serial.printf("Setup Wi-Fi: %s\nSetup password: %s\nOpen http://aquamax.local/setup (or http://192.168.4.1/setup)\n", name, password);
}

bool DashboardPreview::onSetupNetwork() {
    return setupActive_ && server_.client().localIP() == WiFi.softAPIP();
}

void DashboardPreview::serveSetup() {
    if (!onSetupNetwork()) {
        server_.send(404, "text/plain", "Not found");
        return;
    }
    String page(FPSTR(setupPage));
    page.replace("__SETUP_TOKEN__", setupToken_);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "text/html", page);
}

void DashboardPreview::serveSetupNetworks() {
    if (!onSetupNetwork()) { server_.send(404, "text/plain", "Not found"); return; }
    server_.sendHeader("Cache-Control", "no-store");
    if (attempting_ || connectQueued_) { server_.send(409, "application/json", "{\"state\":\"connecting\"}"); return; }
    int count = WiFi.scanComplete();
    if (count != WIFI_SCAN_RUNNING && (count == WIFI_SCAN_FAILED || server_.hasArg("refresh"))) {
        WiFi.scanDelete();
        count = WiFi.scanNetworks(true);
    }
    if (count == WIFI_SCAN_RUNNING) { server_.send(200, "application/json", "{\"state\":\"scanning\"}"); return; }
    if (count < 0) { server_.send(503, "application/json", "{\"state\":\"error\"}"); return; }
    String response = F("{\"state\":\"ready\",\"networks\":[");
    bool first = true;
    for (int index = 0; index < count; ++index) {
        const String ssid = WiFi.SSID(index);
        if (!ssid.length() || ssid.length() > 32) continue;
        bool valid = true;
        for (unsigned character = 0; character < ssid.length(); ++character) if (static_cast<unsigned char>(ssid[character]) < 0x20) valid = false;
        for (int previous = 0; previous < index; ++previous) if (WiFi.SSID(previous) == ssid) valid = false;
        if (!valid) continue;
        if (!first) response += ',';
        first = false;
        response += F("{\"ssid\":"); response += jsonText(ssid.c_str());
        response += F(",\"rssi\":"); response += String(WiFi.RSSI(index));
        response += F(",\"secure\":"); response += WiFi.encryptionType(index) == WIFI_AUTH_OPEN ? "false" : "true";
        response += '}';
    }
    response += F("]}");
    server_.send(200, "application/json", response);
}

void DashboardPreview::serveSetupStatus() {
    if (!onSetupNetwork()) {
        server_.send(404, "text/plain", "Not found");
        return;
    }
    String response = F("{\"state\":\"");
    response += connected_ ? "connected" : connectQueued_ || attempting_ ? "connecting" : setupFailed_ ? "failed" : "idle";
    response += F("\",\"ip\":\"");
    if (connected_) response += WiFi.localIP().toString();
    response += F("\",\"reason\":");
    response += String(setupFailed_ ? lastDisconnectReason_ : 0);
    response += '}';
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::connectNetwork() {
    if (!onSetupNetwork() || !server_.hasArg("token") || server_.arg("token") != setupToken_) {
        server_.send(403, "text/plain", "Setup request rejected");
        return;
    }
    const uint32_t now = millis();
    if (submitted_ && now - lastSubmit_ < submitIntervalMs) {
        server_.send(429, "text/plain", "Wait before retrying");
        return;
    }
    submitted_ = true;
    lastSubmit_ = now;
    const String ssid = server_.arg("ssid");
    const String password = server_.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63 ||
        (password.length() > 0 && password.length() < 8)) {
        server_.send(400, "text/plain", "Invalid network name or password length");
        return;
    }
    pendingSsid_ = ssid;
    pendingPassword_ = password;
    connectRequested_ = now;
    connectQueued_ = true;
    attempting_ = false;
    setupFailed_ = false;
    lastDisconnectReason_ = 0;
    disconnectReason_.store(0);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(202, "text/html", F("<!doctype html><html lang='en'><meta name='viewport' content='width=device-width,initial-scale=1'><title>AquaMax Wi-Fi setup</title><h1>Connecting</h1><p id='status'>Connecting to your network. The setup Wi-Fi may disconnect briefly; reconnect to it to see the result.</p><p><a href='/setup'>Return to setup</a></p><script>async function check(){try{const r=await fetch('/setup/status',{cache:'no-store'});if(!r.ok)return;const s=await r.json();const e=document.getElementById('status');if(s.state==='connected'){e.textContent='Connected. Join your regular Wi-Fi and open http://'+s.ip;}else if(s.state==='failed'){e.textContent='Connection failed (reason '+s.reason+'). Return to setup and try again.'}}catch{}}setInterval(check,2000);check();</script></html>"));
}

void DashboardPreview::serveAsset(const char* path, const char* contentType) {
    if (filesystemReady_) {
        String compressedPath = String(path) + ".gz";
        File file = LittleFS.open(compressedPath, "r");
        if (file) {
            server_.sendHeader("Cache-Control", "no-cache");
            server_.streamFile(file, contentType);
            file.close();
            return;
        }
    }
    server_.send(503, "text/plain", "Dashboard files missing. Build and upload the LittleFS image.");
}

void DashboardPreview::uploadAsset() {
    HTTPUpload& upload = server_.upload();
    constexpr std::size_t maxAssetBytes = 262144;
    const char* temporary = "/littlefs/.dashboard-upload.tmp";
    if (upload.status == UPLOAD_FILE_START) {
        if (assetUpload_) { std::fclose(assetUpload_); assetUpload_ = nullptr; }
        assetUploadSucceeded_ = false;
        assetUploadBytes_ = 0;
        assetUploadName_ = upload.filename;
        if (!filesystemReady_ || !allowedOrigin(server_.header("Origin")) ||
            server_.clientContentLength() <= 0 || server_.clientContentLength() > static_cast<int>(maxAssetBytes + 4096) ||
            (assetUploadName_ != "index.html.gz" && assetUploadName_ != "app.js.gz" && assetUploadName_ != "app.css.gz" &&
             assetUploadName_ != "manifest.webmanifest.gz" && assetUploadName_ != "service-worker.js.gz" &&
             assetUploadName_ != "icon-192.png.gz" && assetUploadName_ != "icon-512.png.gz" &&
             assetUploadName_ != "logo-aquamax.svg.gz" && assetUploadName_ != "jumbo.jpg.gz")) return;
        assetUpload_ = std::fopen(temporary, "w+b");
    } else if (upload.status == UPLOAD_FILE_WRITE && assetUpload_) {
        assetUploadBytes_ += upload.currentSize;
        if (assetUploadBytes_ > maxAssetBytes || std::fwrite(upload.buf, 1, upload.currentSize, assetUpload_) != upload.currentSize) {
            std::fclose(assetUpload_);
            assetUpload_ = nullptr;
            std::remove(temporary);
        }
    } else if (upload.status == UPLOAD_FILE_END && assetUpload_) {
        uint8_t header[2] = {};
        const bool valid = assetUploadBytes_ > 2 && std::fflush(assetUpload_) == 0 && fsync(fileno(assetUpload_)) == 0 &&
                           std::fseek(assetUpload_, 0, SEEK_SET) == 0 && std::fread(header, 1, 2, assetUpload_) == 2 &&
                           header[0] == 0x1f && header[1] == 0x8b;
        const bool closed = std::fclose(assetUpload_) == 0;
        assetUpload_ = nullptr;
        const String destination = String("/littlefs/") + assetUploadName_;
        assetUploadSucceeded_ = valid && closed && std::rename(temporary, destination.c_str()) == 0;
        if (!assetUploadSucceeded_) std::remove(temporary);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (assetUpload_) std::fclose(assetUpload_);
        assetUpload_ = nullptr;
        assetUploadSucceeded_ = false;
        std::remove(temporary);
    }
}

void DashboardPreview::setTimezone() {
    if (!allowedOrigin(server_.header("Origin"))) {
        server_.send(403, "text/plain", "Request rejected");
        return;
    }
    const TimeZone* selected = findTimeZone(server_.arg("timezone").c_str());
    if (!selected) {
        server_.send(400, "text/plain", "Unknown timezone");
        return;
    }
    Preferences storage;
    if (!storage.begin("clock", false)) {
        server_.send(503, "text/plain", "Timezone storage unavailable");
        return;
    }
    const bool saved = storage.putString("timezone", selected->name) == strlen(selected->name);
    storage.end();
    if (!saved) {
        server_.send(503, "text/plain", "Timezone could not be saved");
        return;
    }
    timezone_ = selected;
    setenv("TZ", timezone_->rule, 1);
    tzset();
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::serveMonitoring() {
    const uint8_t mask = monitoringMask_.load();
    String response = F("{\"temperature\":"); response += mask & monitorTemperature ? "true" : "false";
    response += F(",\"ph\":"); response += mask & monitorPh ? "true" : "false";
    response += F(",\"tds\":"); response += mask & monitorTds ? "true" : "false";
#if defined(AQUAMAX_PH_PROBE)
    response += F(",\"phAvailable\":true");
#else
    response += F(",\"phAvailable\":false");
#endif
#if defined(AQUAMAX_TDS_PROBE)
    response += F(",\"tdsAvailable\":true");
#else
    response += F(",\"tdsAvailable\":false");
#endif
    response += F(",\"storageError\":"); response += monitoringStorageError_ ? "true" : "false";
    response += '}';
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::changeMonitoring() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    const String temperature = server_.arg("temperature");
    const String ph = server_.arg("ph");
    const String tds = server_.arg("tds");
    if ((temperature != "0" && temperature != "1") || (ph != "0" && ph != "1") || (tds != "0" && tds != "1")) {
        server_.send(400, "text/plain", "Invalid monitoring settings"); return;
    }
#if defined(AQUAMAX_PH_PROBE)
    if (phCalibrationSession_.active()) { server_.send(409, "text/plain", "Finish pH calibration first"); return; }
#endif
#if defined(AQUAMAX_TDS_PROBE)
    if (tdsCalibrationActive_ || !tdsQuiet_) { server_.send(409, "text/plain", "Finish the current TDS operation first"); return; }
#endif
    const uint8_t mask = (temperature == "1" ? monitorTemperature : 0) | (ph == "1" ? monitorPh : 0) | (tds == "1" ? monitorTds : 0);
    const uint8_t previousMask = monitoringMask_.load();
    if (mask == previousMask && !monitoringStorageError_) { serveMonitoring(); return; }
    const uint32_t now = millis();
    if (monitoringWritten_ && now - lastMonitoringWriteMs_ < systemWriteIntervalMs) { server_.send(429, "text/plain", "Try again shortly"); return; }
    Preferences storage;
    if (!storage.begin("aquamax", false)) { server_.send(503, "text/plain", "Monitoring settings unavailable"); return; }
    const bool saved = storage.putUChar("monitor", mask) == sizeof(mask);
    storage.end();
    monitoringWritten_ = true;
    lastMonitoringWriteMs_ = now;
    if (!saved) { monitoringStorageError_ = true; server_.send(503, "text/plain", "Monitoring settings could not be saved"); return; }
    monitoringStorageError_ = false;
    monitoringMask_.store(mask);
#if defined(AQUAMAX_PH_PROBE)
    if (!(mask & monitorPh)) {
        if (phConversionPending_) phAdc_.cancel();
        phConversionPending_ = false;
        phAdcHasValue_ = false;
        phChannel_ = PhChannel{};
        phReading_ = PhReading{};
    }
#endif
#if defined(AQUAMAX_TDS_PROBE)
    if (!(mask & monitorTds)) { tdsHasVoltage_ = false; tdsStable_ = false; }
#endif
    portENTER_CRITICAL(&snapshotMutex_);
    if (!(mask & monitorTemperature)) snapshot_.temperature = {ProbeState::disabled, false, 0.0f, 0};
    if (!(mask & monitorPh)) snapshot_.ph = {ProbeState::disabled, false, 0.0f, 0};
    else if (!(previousMask & monitorPh)) snapshot_.ph = {ProbeState::waiting, false, 0.0f, 0};
    if (!(mask & monitorTds)) snapshot_.tds = {ProbeState::disabled, false, 0.0f, 0};
    else if (!(previousMask & monitorTds)) snapshot_.tds = {ProbeState::waiting, false, 0.0f, 0};
    portEXIT_CRITICAL(&snapshotMutex_);
    serveMonitoring();
}

String DashboardPreview::stateJson() {
    portENTER_CRITICAL(&snapshotMutex_);
    const SensorSnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(&snapshotMutex_);
    String response = F("{\"temperature\":{\"state\":\"");
    response += stateName(snapshot.temperature.state);
    response += F("\",\"valueC\":");
    response += snapshot.temperature.hasValue ? String(snapshot.temperature.valueC, 2) : String("null");
    response += F("},\"ph\":{\"state\":\"");
    response += stateName(snapshot.ph.state);
    response += F("\",\"value\":");
    response += snapshot.ph.hasValue && millis() - snapshot.ph.lastSeenMs < 3000 ? String(snapshot.ph.value, 2) : String("null");
#if defined(AQUAMAX_PH_PROBE)
    response += F(",\"calibrated\":"); response += snapshot.ph.calibrated ? "true" : "false";
    response += F(",\"calibrationError\":"); response += snapshot.ph.calibrationError ? "true" : "false";
#endif
    response += F("},\"tds\":{\"state\":\"");
    response += stateName(snapshot.tds.state);
    response += F("\",\"value\":");
#if defined(AQUAMAX_TDS_PROBE)
    if (snapshot.tds.hasValue && millis() - snapshot.tds.lastSeenMs < 75000) {
        char value[20] = {};
        std::snprintf(value, sizeof(value), "%.1f", snapshot.tds.value);
        response += value;
    } else {
        response += F("null");
    }
    response += F(",\"calibrated\":"); response += tdsCalibration_.calibrated && !tdsCalibrationError_ ? "true" : "false";
    response += F(",\"defaultTemperature\":"); response += tdsUsedDefaultTemperature_ ? "true" : "false";
#else
    response += F("null");
#endif
    response += F("},\"wifi\":\"");
    response += WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
    response += F("\",\"uptimeSeconds\":");
    response += String(millis() / 1000);
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    response += F(",\"time\":{\"synchronized\":");
    response += clock.synchronized ? "true" : "false";
    response += F(",\"unixSeconds\":");
    response += clock.synchronized ? String(clock.unixSeconds) : String("null");
    response += F(",\"local\":");
    if (clock.synchronized) {
        const time_t timestamp = static_cast<time_t>(clock.unixSeconds);
        struct tm local{};
        char formatted[20] = {};
        if (localtime_r(&timestamp, &local) && strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &local)) {
            response += '"';
            response += formatted;
            response += '"';
        } else response += F("null");
    } else response += F("null");
    response += F(",\"timezone\":\"");
    response += timezone_->name;
    response += '"';
    response += '}';
    response += F(",\"alerts\":{\"temperature\":\"");
    response += alertStateName(temperatureAlertState_);
    response += F("\",\"event\":\"");
    response += alertEventName(lastAlertEvent_);
    response += F("\",\"sequence\":");
    response += String(alertSequence_);
    response += F(",\"settingsError\":");
    response += alertSettingsError_ ? "true" : "false";
    response += F(",\"ph\":\"");
#if defined(AQUAMAX_PH_PROBE)
    response += probeWarningName(phCalibrationSession_.active() ? ProbeWarning::normal : phWarnings_.state());
#else
    response += F("normal");
#endif
    response += F("\",\"tds\":\"");
#if defined(AQUAMAX_TDS_PROBE)
    response += probeWarningName(tdsCalibrationActive_ ? ProbeWarning::normal : tdsWarnings_.state());
#else
    response += F("normal");
#endif
    response += '"';
    response += '}';
    response += '}';
    return response;
}

void DashboardPreview::serveState() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", stateJson());
}

void DashboardPreview::serveSystem() {
    String response = F("{\"uptimeSeconds\":");
    response += String(millis() / 1000);
    response += F(",\"freeHeapBytes\":"); response += String(ESP.getFreeHeap());
    response += F(",\"freePsramBytes\":"); response += String(ESP.getFreePsram());
    response += F(",\"psramBytes\":"); response += String(ESP.getPsramSize());
    response += F(",\"flashBytes\":"); response += String(ESP.getFlashChipSize());
    response += F(",\"filesystemReady\":"); response += filesystemReady_ ? "true" : "false";
    response += F(",\"filesystemUsedBytes\":"); response += String(filesystemReady_ ? LittleFS.usedBytes() : 0);
    response += F(",\"filesystemTotalBytes\":"); response += String(filesystemReady_ ? LittleFS.totalBytes() : 0);
    response += F(",\"journalReady\":"); response += journalReady_ ? "true" : "false";
#if defined(AQUAMAX_PH_PROBE)
    response += F(",\"phAdc\":{\"status\":");
    response += phAdcError_ ? "\"error\"" : phAdcHasValue_ ? "\"ready\"" : "\"waiting\"";
    response += F(",\"channel\":1,\"adcVolts\":");
    response += phAdcHasValue_ ? String(phAdcVolts_, 4) : String("null");
    response += '}';
#endif
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    response += F(",\"tdsDiagnostic\":\"");
    response += tdsDiagnosticActive_ ? "active" : tdsDiagnosticRecovering_ ? "recovery" : tdsDiagnosticTimer_ ? "ready" : "unavailable";
    response += '"';
    response += F(",\"tdsAdc\":{\"status\":");
    response += tdsDiagnosticAdcError_ ? "\"error\"" : tdsDiagnosticHasValue_ ? "\"ready\"" : "\"waiting\"";
    response += F(",\"channel\":2,\"adcVolts\":");
    response += tdsDiagnosticHasValue_ ? String(tdsDiagnosticVolts_, 4) : String("null");
    response += '}';
#endif
#if defined(AQUAMAX_TFT)
    response += F(",\"tftRenderer\":"); response += tftRendererReady_ ? "\"initialized\"" : "\"unavailable\"";
#endif
    response += F(",\"wifiConnected\":"); response += WiFi.status() == WL_CONNECTED ? "true" : "false";
    response += F(",\"wifiRssi\":"); response += WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : String("null");
    response += F(",\"ipAddress\":"); response += WiFi.status() == WL_CONNECTED ? jsonText(WiFi.localIP().toString().c_str()) : String("null");
    response += '}';
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::serveConfiguration() {
    if (alertSettingsError_ || temperatureCalibrationError_) { server_.send(503, "text/plain", "Configuration storage unavailable"); return; }
    String response = F("{\"version\":1,\"timezone\":");
    response += jsonText(timezone_->name);
    response += F(",\"alerts\":{\"thresholdsEnabled\":"); response += alertConfig_.thresholdsEnabled ? "true" : "false";
    response += F(",\"probeFaultEnabled\":"); response += alertConfig_.probeFaultEnabled ? "true" : "false";
    response += F(",\"minimum\":"); response += String(alertConfig_.minimum, 6);
    response += F(",\"maximum\":"); response += String(alertConfig_.maximum, 6);
    response += F(",\"hysteresis\":"); response += String(alertConfig_.hysteresis, 6);
    response += F(",\"debounceSeconds\":"); response += String(alertConfig_.debounceMs / 1000);
    response += F(",\"repeatSeconds\":"); response += String(alertConfig_.repeatMs / 1000);
    response += F("},\"temperatureCalibration\":{\"offsetC\":"); response += String(temperatureCalibration_.offsetC, 6);
    response += F(",\"timestamp\":"); response += String(temperatureCalibration_.timestamp);
    response += F(",\"calibrated\":"); response += temperatureCalibration_.calibrated ? "true" : "false";
    response += F("}}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("Content-Disposition", "attachment; filename=\"aquamax-config.json\"");
    server_.send(200, "application/json", response);
}

void DashboardPreview::restoreConfiguration() {
    if (!allowedOrigin(server_.header("Origin")) || server_.arg("confirm") != "yes") { server_.send(403, "text/plain", "Request rejected"); return; }
    const uint32_t now = millis();
    if (rebootPending_ || (systemWritten_ && now - lastSystemWriteMs_ < systemWriteIntervalMs) ||
        (alertWritten_ && now - lastAlertWriteMs_ < alertWriteIntervalMs) ||
        (calibrationWritten_ && now - lastCalibrationWriteMs_ < calibrationWriteIntervalMs)) { server_.send(429, "text/plain", "Try again shortly"); return; }
    const TimeZone* selected = findTimeZone(server_.arg("timezone").c_str());
    const String enabled = server_.arg("thresholdsEnabled"), fault = server_.arg("probeFaultEnabled"), calibrated = server_.arg("calibrated");
    const String timestamp = server_.arg("timestamp");
    AlertConfig alerts;
    TemperatureCalibration calibration;
    if (server_.clientContentLength() <= 0 || server_.clientContentLength() > 1024 || server_.arg("version") != "1" || !selected ||
        (enabled != "0" && enabled != "1") || (fault != "0" && fault != "1") || (calibrated != "0" && calibrated != "1") ||
        !parseFloat(server_.arg("minimum"), alerts.minimum) || !parseFloat(server_.arg("maximum"), alerts.maximum) ||
        !parseFloat(server_.arg("hysteresis"), alerts.hysteresis) || !parseSeconds(server_.arg("debounceSeconds"), 3600, alerts.debounceMs) ||
        !parseSeconds(server_.arg("repeatSeconds"), 86400, alerts.repeatMs) || !parseFloat(server_.arg("offsetC"), calibration.offsetC) ||
        !timestamp.length() || timestamp.length() > 10) { server_.send(400, "text/plain", "Invalid configuration"); return; }
    for (unsigned index = 0; index < timestamp.length(); ++index) if (timestamp[index] < '0' || timestamp[index] > '9') { server_.send(400, "text/plain", "Invalid configuration"); return; }
    const unsigned long long parsedTimestamp = strtoull(timestamp.c_str(), nullptr, 10);
    if (parsedTimestamp > UINT32_MAX) { server_.send(400, "text/plain", "Invalid configuration"); return; }
    alerts.thresholdsEnabled = enabled == "1";
    alerts.probeFaultEnabled = fault == "1";
    calibration.calibrated = calibrated == "1";
    calibration.timestamp = static_cast<uint32_t>(parsedTimestamp);
    if (!validAlertConfig(alerts) || !validTemperatureCalibration(calibration)) { server_.send(400, "text/plain", "Invalid configuration"); return; }
    systemWritten_ = alertWritten_ = calibrationWritten_ = true;
    lastSystemWriteMs_ = lastAlertWriteMs_ = lastCalibrationWriteMs_ = now;
    if (!temperatureCalibrationStore_.save(calibration)) { server_.send(503, "text/plain", "Calibration could not be restored; no further settings were changed"); return; }
    temperatureCalibration_ = calibration;
    temperatureOffset_.store(calibration.offsetC);
    temperatureCalibrationError_ = false;
    if (!alertSettingsStore_.save(alerts)) { alertSettingsError_ = true; server_.send(503, "text/plain", "Calibration restored, but alerts could not be saved"); return; }
    alertConfig_ = alerts;
    alertSettingsError_ = false;
    temperatureAlerts_.reset();
    temperatureAlertState_ = AlertState::normal;
    Preferences storage;
    if (!storage.begin("clock", false)) { server_.send(503, "text/plain", "Calibration and alerts restored, but timezone could not be saved"); return; }
    const bool saved = storage.putString("timezone", selected->name) == strlen(selected->name);
    storage.end();
    if (!saved) { server_.send(503, "text/plain", "Calibration and alerts restored, but timezone could not be saved"); return; }
    timezone_ = selected;
    setenv("TZ", timezone_->rule, 1);
    tzset();
    server_.send(200, "application/json", "{\"ok\":true}");
}

bool DashboardPreview::flushHistory() {
    if (!filesystemReady_) return false;
    if (!historyStarted_) return true;
    const bool temperatureSaved = temperatureHistory_.flush();
    const bool phSaved = phHistory_.flush();
    const bool tdsSaved = tdsHistory_.flush();
    return temperatureSaved && phSaved && tdsSaved;
}

void DashboardPreview::reboot() {
    if (!allowedOrigin(server_.header("Origin")) || server_.arg("confirm") != "yes") { server_.send(403, "text/plain", "Request rejected"); return; }
    const uint32_t now = millis();
    if (rebootPending_ || (systemWritten_ && now - lastSystemWriteMs_ < systemWriteIntervalMs)) { server_.send(429, "text/plain", "Try again shortly"); return; }
    systemWritten_ = true;
    lastSystemWriteMs_ = now;
    if (!flushHistory()) { server_.send(503, "text/plain", "History could not be saved"); return; }
    server_.send(200, "application/json", "{\"ok\":true}");
    rebootPending_ = true;
    rebootRequestedMs_ = millis();
}

void DashboardPreview::uploadFirmware() {
    HTTPUpload& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        firmwareUploadSucceeded_ = false;
        firmwareUploadActive_ = false;
        firmwareUploadBytes_ = 0;
        const uint32_t now = millis();
        const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
        if (!allowedOrigin(server_.header("Origin")) || server_.arg("confirm") != "yes" || rebootPending_ ||
            (systemWritten_ && now - lastSystemWriteMs_ < systemWriteIntervalMs) || !partition ||
            server_.clientContentLength() <= 0 || server_.clientContentLength() > static_cast<int>(partition->size + 4096) ||
            !upload.filename.endsWith(".bin")) return;
        systemWritten_ = true;
        lastSystemWriteMs_ = now;
        firmwarePartition_ = partition;
        if (flushHistory()) firmwareUploadActive_ = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &firmwareHandle_) == ESP_OK;
    } else if (upload.status == UPLOAD_FILE_WRITE && firmwareUploadActive_) {
        firmwareUploadBytes_ += upload.currentSize;
        if (firmwareUploadBytes_ > firmwarePartition_->size || esp_ota_write(firmwareHandle_, upload.buf, upload.currentSize) != ESP_OK) {
            esp_ota_abort(firmwareHandle_);
            firmwareUploadActive_ = false;
        }
    } else if (upload.status == UPLOAD_FILE_END && firmwareUploadActive_) {
        const esp_err_t result = esp_ota_end(firmwareHandle_);
        firmwareUploadSucceeded_ = firmwareUploadBytes_ > 0 && result == ESP_OK && esp_ota_set_boot_partition(firmwarePartition_) == ESP_OK;
        firmwareUploadActive_ = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (firmwareUploadActive_) esp_ota_abort(firmwareHandle_);
        firmwareUploadActive_ = false;
        firmwareUploadSucceeded_ = false;
    }
}

void DashboardPreview::updateAlerts(uint32_t now) {
    portENTER_CRITICAL(&snapshotMutex_);
    const SensorSnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(&snapshotMutex_);
    const bool fresh = snapshot.temperature.hasValue && millis() - snapshot.temperature.lastSeenMs < 3000;
    ProbeState probe = snapshot.temperature.state;
    if (probe == ProbeState::connected && snapshot.temperature.hasValue && !fresh) probe = ProbeState::unstable;
    const AlertState previous = temperatureAlertState_;
    const AlertResult result = temperatureAlerts_.update(now, probe, fresh, snapshot.temperature.valueC, alertConfig_);
    temperatureAlertState_ = result.state;
    displayTemperatureAlert_.store(result.state != AlertState::normal);
    if (result.event != AlertEvent::none) {
        lastAlertEvent_ = result.event;
        ++alertSequence_;
        if (result.state != previous && journalReady_) {
            const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
            const char* text = result.event == AlertEvent::low ? "Temperature below minimum" :
                               result.event == AlertEvent::high ? "Temperature above maximum" :
                               result.event == AlertEvent::probeFault ? "Temperature probe fault" : "Temperature alert recovered";
            journalReady_ = eventJournal_.add(clock.synchronized ? clock.unixSeconds : 0,
                                              result.event == AlertEvent::recovered ? JournalKind::recovery : JournalKind::alert, text);
        }
    }
#if defined(AQUAMAX_PH_PROBE)
    bool phPaused = phCalibrationSession_.active();
#if defined(AQUAMAX_TDS_PROBE)
    phPaused = phPaused || !tdsQuiet_;
#endif
    const bool phChanged = phWarnings_.update(now, snapshot.ph.state, phPaused, 10000);
    if (phChanged && (monitoringMask_.load() & monitorPh) && journalReady_) {
        const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
        const ProbeWarning warning = phWarnings_.state();
        const char* text = warning == ProbeWarning::signalFault ? "pH signal or connection fault" :
                           warning == ProbeWarning::outOfRange ? "pH measurement out of range" :
                           warning == ProbeWarning::unstable ? "pH measurement unstable" : "pH signal recovered";
        journalReady_ = eventJournal_.add(clock.synchronized ? clock.unixSeconds : 0,
                                          warning == ProbeWarning::normal ? JournalKind::recovery : JournalKind::alert, text);
    }
#endif
#if defined(AQUAMAX_TDS_PROBE)
    const bool tdsPaused = tdsCalibrationActive_ || phCalibrationSession_.active() || !tdsQuiet_;
    const bool tdsChanged = tdsWarnings_.update(now, snapshot.tds.state, tdsPaused, 90000);
    if (tdsChanged && (monitoringMask_.load() & monitorTds) && journalReady_) {
        const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
        const ProbeWarning warning = tdsWarnings_.state();
        const char* text = warning == ProbeWarning::signalFault ? "TDS signal or connection fault" :
                           warning == ProbeWarning::outOfRange ? "TDS measurement out of range" :
                           warning == ProbeWarning::unstable ? "TDS measurement unstable" : "TDS signal recovered";
        journalReady_ = eventJournal_.add(clock.synchronized ? clock.unixSeconds : 0,
                                          warning == ProbeWarning::normal ? JournalKind::recovery : JournalKind::alert, text);
    }
#endif
}

void DashboardPreview::serveAlerts() {
    String response = F("{\"temperature\":{\"thresholdsEnabled\":");
    response += alertConfig_.thresholdsEnabled ? "true" : "false";
    response += F(",\"probeFaultEnabled\":");
    response += alertConfig_.probeFaultEnabled ? "true" : "false";
    response += F(",\"minimum\":"); response += String(alertConfig_.minimum, 2);
    response += F(",\"maximum\":"); response += String(alertConfig_.maximum, 2);
    response += F(",\"hysteresis\":"); response += String(alertConfig_.hysteresis, 2);
    response += F(",\"debounceSeconds\":"); response += String(alertConfig_.debounceMs / 1000);
    response += F(",\"repeatSeconds\":"); response += String(alertConfig_.repeatMs / 1000);
    response += F(",\"state\":\""); response += alertStateName(temperatureAlertState_);
    response += F("\"},\"settingsError\":"); response += alertSettingsError_ ? "true" : "false";
    response += F(",\"ph\":\""); response += probeWarningName(phWarnings_.state());
    response += F("\",\"tds\":\""); response += probeWarningName(tdsWarnings_.state());
    response += F("\"}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::changeAlerts() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    const uint32_t now = millis();
    if (alertWritten_ && now - lastAlertWriteMs_ < alertWriteIntervalMs) { server_.send(429, "text/plain", "Try again shortly"); return; }
    const String enabled = server_.arg("thresholdsEnabled");
    const String fault = server_.arg("probeFaultEnabled");
    AlertConfig candidate;
    if ((enabled != "0" && enabled != "1") || (fault != "0" && fault != "1") ||
        !parseFloat(server_.arg("minimum"), candidate.minimum) || !parseFloat(server_.arg("maximum"), candidate.maximum) ||
        !parseFloat(server_.arg("hysteresis"), candidate.hysteresis) ||
        !parseSeconds(server_.arg("debounceSeconds"), 3600, candidate.debounceMs) ||
        !parseSeconds(server_.arg("repeatSeconds"), 86400, candidate.repeatMs)) {
        server_.send(400, "text/plain", "Invalid alert settings"); return;
    }
    candidate.thresholdsEnabled = enabled == "1";
    candidate.probeFaultEnabled = fault == "1";
    if (!validAlertConfig(candidate)) { server_.send(400, "text/plain", "Invalid alert settings"); return; }
    alertWritten_ = true;
    lastAlertWriteMs_ = now;
    if (!alertSettingsStore_.save(candidate)) { alertSettingsError_ = true; server_.send(503, "text/plain", "Alert settings could not be saved"); return; }
    alertConfig_ = candidate;
    alertSettingsError_ = false;
    temperatureAlerts_.reset();
    temperatureAlertState_ = AlertState::normal;
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::serveEvents() {
    if (!journalReady_) { server_.send(503, "text/plain", "Event journal unavailable"); return; }
    String response = F("{\"events\":[");
    for (std::size_t index = 0; index < eventJournal_.size(); ++index) {
        const JournalEntry* entry = eventJournal_.recent(index);
        if (!entry) continue;
        if (index) response += ',';
        response += F("{\"id\":"); response += String(entry->id);
        response += F(",\"timestamp\":"); response += entry->timestamp ? String(entry->timestamp) : String("null");
        response += F(",\"kind\":\"");
        response += entry->kind == JournalKind::note ? "note" : entry->kind == JournalKind::alert ? "alert" : "recovery";
        response += F("\",\"text\":"); response += jsonText(entry->text);
        response += '}';
    }
    response += F("]}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::addEvent() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    if (!journalReady_) { server_.send(503, "text/plain", "Event journal unavailable"); return; }
    const uint32_t now = millis();
    if (journalWritten_ && now - lastJournalWriteMs_ < journalWriteIntervalMs) { server_.send(429, "text/plain", "Try again shortly"); return; }
    const String text = server_.arg("text");
    if (text.length() != std::strlen(text.c_str()) || !validJournalText(text.c_str())) { server_.send(400, "text/plain", "Invalid note"); return; }
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    journalWritten_ = true;
    lastJournalWriteMs_ = now;
    if (!eventJournal_.add(clock.synchronized ? clock.unixSeconds : 0, JournalKind::note, text.c_str())) {
        journalReady_ = false; server_.send(503, "text/plain", "Event journal unavailable"); return;
    }
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::deleteEvent() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    if (!journalReady_) { server_.send(503, "text/plain", "Event journal unavailable"); return; }
    if (server_.arg("confirm") != "yes") { server_.send(400, "text/plain", "Confirmation required"); return; }
    const String text = server_.arg("id");
    if (!text.length() || text.length() > 10) { server_.send(400, "text/plain", "Invalid event"); return; }
    for (unsigned index = 0; index < text.length(); ++index) if (text[index] < '0' || text[index] > '9') { server_.send(400, "text/plain", "Invalid event"); return; }
    const unsigned long long parsed = strtoull(text.c_str(), nullptr, 10);
    if (!parsed || parsed > UINT32_MAX) { server_.send(400, "text/plain", "Invalid event"); return; }
    bool found = false;
    for (std::size_t index = 0; index < eventJournal_.size(); ++index) {
        const JournalEntry* entry = eventJournal_.recent(index);
        if (entry && entry->id == parsed) { found = true; break; }
    }
    if (!found) { server_.send(404, "text/plain", "Event not found"); return; }
    const uint32_t now = millis();
    if (journalWritten_ && now - lastJournalWriteMs_ < journalWriteIntervalMs) { server_.send(429, "text/plain", "Try again shortly"); return; }
    journalWritten_ = true;
    lastJournalWriteMs_ = now;
    if (!eventJournal_.remove(static_cast<uint32_t>(parsed))) { journalReady_ = false; server_.send(503, "text/plain", "Event journal unavailable"); return; }
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::serveTips() {
    portENTER_CRITICAL(&snapshotMutex_);
    const SensorSnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(&snapshotMutex_);
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    const char* tip = nullptr;
    if (temperatureAlertState_ == AlertState::probeFault || snapshot.temperature.state == ProbeState::disconnected)
        tip = "Check the temperature probe, connector and wiring.";
    else if (temperatureAlertState_ == AlertState::low)
        tip = "Temperature is below your configured range. Check the heater and water circulation.";
    else if (temperatureAlertState_ == AlertState::high)
        tip = "Temperature is above your configured range. Check heating and cooling.";
    else if (clock.synchronized && temperatureCalibration_.calibrated && temperatureCalibration_.timestamp &&
             clock.unixSeconds >= temperatureCalibration_.timestamp && clock.unixSeconds - temperatureCalibration_.timestamp >= 90u * 86400u)
        tip = "Check the temperature probe against a trusted reference thermometer.";
    String response = F("{\"tips\":[");
    if (tip) response += jsonText(tip);
    response += F("]}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::updateHistory(uint32_t now) {
    if (!filesystemReady_) return;
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    if (!clock.synchronized) return;
    if (!historyStarted_ || now - lastHistorySampleMs_ >= historySampleMs) {
        if (!historyStarted_) { historyStarted_ = true; lastHistoryFlushMs_ = now; }
        lastHistorySampleMs_ = now;
        portENTER_CRITICAL(&snapshotMutex_);
        const SensorSnapshot snapshot = snapshot_;
        portEXIT_CRITICAL(&snapshotMutex_);
        const bool validTemperature = snapshot.temperature.hasValue && millis() - snapshot.temperature.lastSeenMs < 3000;
        if (!temperatureHistory_.sample(clock.unixSeconds, validTemperature, snapshot.temperature.valueC) ||
            !phHistory_.sample(clock.unixSeconds, snapshot.ph.hasValue && millis() - snapshot.ph.lastSeenMs < 3000, snapshot.ph.value) ||
#if defined(AQUAMAX_TDS_PROBE)
            !tdsHistory_.sample(clock.unixSeconds, snapshot.tds.hasValue && tdsCalibration_.calibrated && !tdsCalibrationActive_ &&
                                millis() - snapshot.tds.lastSeenMs < 75000, snapshot.tds.value)) {
#else
            !tdsHistory_.sample(clock.unixSeconds, false, 0)) {
#endif
            Serial.println("History: sampling failed; check storage.");
        }
    }
    if (historyStarted_ && now - lastHistoryFlushMs_ >= historyFlushMs) {
        lastHistoryFlushMs_ = now;
        const bool temperatureSaved = temperatureHistory_.flush();
        const bool phSaved = phHistory_.flush();
        const bool tdsSaved = tdsHistory_.flush();
        if (!temperatureSaved || !phSaved || !tdsSaved) Serial.println("History: storage write failed; retrying.");
    }
}

void DashboardPreview::serveHistoryStatus() {
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    const bool healthy = filesystemReady_ && temperatureHistory_.healthy() && phHistory_.healthy() && tdsHistory_.healthy();
    String response = F("{\"state\":\"");
    response += !healthy ? "storage_error" : !clock.synchronized ? "waiting_time" : "ready";
    response += F("\",\"usedBytes\":");
    response += String(filesystemReady_ ? LittleFS.usedBytes() : 0);
    response += F(",\"totalBytes\":");
    response += String(filesystemReady_ ? LittleFS.totalBytes() : 0);
    response += F(",\"retention\":[\"24h at 10s\",\"7d at 1m\",\"30d at 5m\",\"1y at 1h\"]}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

void DashboardPreview::clearHistory() {
    if (!allowedOrigin(server_.header("Origin")) || server_.arg("confirm") != "yes") {
        server_.send(403, "text/plain", "Request rejected");
        return;
    }
    if (!filesystemReady_) { server_.send(503, "text/plain", "History storage unavailable"); return; }
    const bool cleared = historyStorage_.clear();
    temperatureHistory_.reset();
    phHistory_.reset();
    tdsHistory_.reset();
    historyStarted_ = false;
    server_.send(cleared ? 200 : 503, "application/json", cleared ? "{\"ok\":true}" : "{\"ok\":false}");
}

void DashboardPreview::serveCalibration() {
    portENTER_CRITICAL(&snapshotMutex_);
    const SensorSnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(&snapshotMutex_);
    const bool fresh = snapshot.temperature.hasValue && millis() - snapshot.temperature.lastSeenMs < 3000;
    String response = F("{\"temperature\":{\"state\":\"");
    response += stateName(snapshot.temperature.state);
    response += F("\",\"valueC\":");
    response += fresh ? String(snapshot.temperature.valueC, 2) : String("null");
    response += F(",\"uncalibratedC\":");
    response += fresh ? String(snapshot.temperature.uncalibratedC, 2) : String("null");
    response += F(",\"stable\":");
    response += fresh && snapshot.temperature.stable ? "true" : "false";
    response += F(",\"lastSeenSecondsAgo\":");
    response += snapshot.temperature.lastSeenMs ? String((millis() - snapshot.temperature.lastSeenMs) / 1000) : String("null");
    response += F(",\"offsetC\":");
    response += String(temperatureCalibration_.offsetC, 3);
    response += F(",\"calibrated\":");
    response += temperatureCalibration_.calibrated ? "true" : "false";
    response += F(",\"timestamp\":");
    response += temperatureCalibration_.timestamp ? String(temperatureCalibration_.timestamp) : String("null");
    response += F(",\"storageError\":");
    response += temperatureCalibrationError_ ? "true" : "false";
    response += F("},\"ph\":{\"state\":\"");
#if defined(AQUAMAX_PH_PROBE)
    response += stateName(phCalibrationSession_.active() ? phReading_.state : snapshot.ph.state);
#else
    response += stateName(snapshot.ph.state);
#endif
    response += '"';
#if defined(AQUAMAX_PH_PROBE)
    response += F(",\"enabled\":"); response += monitoringMask_.load() & monitorPh ? "true" : "false";
    response += F(",\"adcVolts\":");
    response += phAdcHasValue_ ? String(phAdcVolts_, 4) : String("null");
    response += F(",\"boardVolts\":");
    response += phReading_.hasBoardVolts ? String(phReading_.boardVolts, 4) : String("null");
    response += F(",\"stable\":"); response += phReading_.stable && phAdcHasValue_ ? "true" : "false";
    response += F(",\"calibrated\":"); response += phCalibration_.valid && !phCalibrationError_ ? "true" : "false";
    response += F(",\"storageError\":"); response += phCalibrationError_ ? "true" : "false";
    response += F(",\"active\":"); response += phCalibrationSession_.active() ? "true" : "false";
    response += F(",\"points\":[");
    for (std::size_t index = 0; index < phCalibrationSession_.count(); ++index) {
        if (index) response += ',';
        response += F("{\"ph\":"); response += String(phCalibrationSession_.points()[index].ph, 2);
        response += F(",\"boardVolts\":"); response += String(phCalibrationSession_.points()[index].boardVolts, 4);
        response += '}';
    }
    response += ']';
#else
    response += F(",\"enabled\":false");
#endif
    response += F("},\"tds\":{\"state\":\"");
#if defined(AQUAMAX_TDS_PROBE)
    response += stateName(tdsCalibrationActive_ && tdsHasVoltage_ ? (tdsStable_ ? ProbeState::connected : ProbeState::unstable) : snapshot.tds.state);
#else
    response += stateName(snapshot.tds.state);
#endif
    response += '"';
#if defined(AQUAMAX_TDS_PROBE)
    response += F(",\"enabled\":"); response += monitoringMask_.load() & monitorTds ? "true" : "false";
    response += F(",\"adcVolts\":");
    response += tdsHasVoltage_ && millis() - tdsLastSampleMs_ < 75000 ? String(tdsVoltage_, 4) : String("null");
    response += F(",\"stable\":"); response += tdsStable_ && millis() - tdsLastSampleMs_ < 75000 ? "true" : "false";
    response += F(",\"calibrated\":"); response += tdsCalibration_.calibrated && !tdsCalibrationError_ ? "true" : "false";
    response += F(",\"storageError\":"); response += tdsCalibrationError_ ? "true" : "false";
    response += F(",\"active\":"); response += tdsCalibrationActive_ ? "true" : "false";
    response += F(",\"temperatureC\":"); response += String(tdsTemperatureC_, 1);
    response += F(",\"defaultTemperature\":"); response += tdsUsedDefaultTemperature_ ? "true" : "false";
#else
    response += F(",\"enabled\":false");
#endif
    response += F("}}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

#if defined(AQUAMAX_PH_PROBE)
void DashboardPreview::changePhCalibration() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    if (tdsDiagnosticActive_ || tdsDiagnosticRecovering_) { server_.send(409, "text/plain", "TDS diagnostic in progress"); return; }
#endif
#if defined(AQUAMAX_TDS_PROBE)
    if (!tdsQuiet_ && server_.arg("action") != "cancel") { server_.send(409, "text/plain", "TDS measurement in progress"); return; }
    if (tdsCalibrationActive_ && server_.arg("action") != "cancel") { server_.send(409, "text/plain", "TDS calibration in progress"); return; }
#endif
    const String action = server_.arg("action");
    if (action == "start") {
        if (!(monitoringMask_.load() & monitorPh)) { server_.send(409, "text/plain", "pH monitoring is disabled"); return; }
        if (phCalibrationError_) { server_.send(503, "text/plain", "pH calibration storage unavailable"); return; }
        phCalibrationSession_.start();
        phChannel_ = PhChannel{};
        phReading_ = PhReading{};
        portENTER_CRITICAL(&snapshotMutex_);
        snapshot_.ph.hasValue = false;
        portEXIT_CRITICAL(&snapshotMutex_);
    } else if (action == "cancel") {
        phCalibrationSession_.cancel();
    } else if (action == "capture") {
        float reference = 0.0f;
        if (!parseFloat(server_.arg("referencePh"), reference)) { server_.send(400, "text/plain", "Invalid buffer pH"); return; }
        if (!phCalibrationSession_.active() || !phAdcHasValue_ || !phReading_.hasBoardVolts || !phReading_.stable ||
            phReading_.state != ProbeState::connected || millis() - lastPhSampleMs_ >= 3000) {
            server_.send(409, "text/plain", "pH signal is not ready and stable"); return;
        }
        if (!phCalibrationSession_.capture(reference, phReading_.boardVolts, phVoltageLimits_)) {
            server_.send(400, "text/plain", "Invalid, too-close, or too many buffer points"); return;
        }
    } else if (action == "save") {
        const uint32_t now = millis();
        if (phCalibrationError_) { server_.send(503, "text/plain", "pH calibration storage unavailable"); return; }
        if (phCalibrationWritten_ && now - lastPhCalibrationWriteMs_ < calibrationWriteIntervalMs) {
            server_.send(429, "text/plain", "Try again shortly"); return;
        }
        PhCalibration candidate;
        if (!phCalibrationSession_.fit(phVoltageLimits_, phCalibration_, candidate)) {
            server_.send(400, "text/plain", "At least two valid buffer points with a decreasing voltage are required"); return;
        }
        phCalibrationWritten_ = true;
        lastPhCalibrationWriteMs_ = now;
        if (!phCalibrationStore_.commit(phCalibrationSession_.points(), phCalibrationSession_.count(), phVoltageLimits_, phCalibration_)) {
            server_.send(503, "text/plain", "pH calibration could not be saved"); return;
        }
        phCalibrationSession_.cancel();
    } else {
        server_.send(400, "text/plain", "Invalid action"); return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", "{\"ok\":true}");
}
#endif

void DashboardPreview::changeTemperatureCalibration() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    const uint32_t now = millis();
    if (calibrationWritten_ && now - lastCalibrationWriteMs_ < calibrationWriteIntervalMs) {
        server_.send(429, "text/plain", "Try again shortly"); return;
    }
    TemperatureCalibration candidate;
    const String action = server_.arg("action");
    if (action == "capture") {
        if (temperatureCalibrationError_) { server_.send(503, "text/plain", "Calibration storage unavailable"); return; }
        portENTER_CRITICAL(&snapshotMutex_);
        const SensorSnapshot snapshot = snapshot_;
        portEXIT_CRITICAL(&snapshotMutex_);
        if (!snapshot.temperature.hasValue || !snapshot.temperature.stable || millis() - snapshot.temperature.lastSeenMs >= 3000) {
            server_.send(409, "text/plain", "Temperature probe is not connected and stable"); return;
        }
        const String reference = server_.arg("referenceC");
        char* end = nullptr;
        const float referenceC = strtof(reference.c_str(), &end);
        if (reference.length() == 0 || !end || *end != '\0' || !std::isfinite(referenceC) || referenceC < -55.0f || referenceC > 125.0f) {
            server_.send(400, "text/plain", "Invalid reference temperature"); return;
        }
        candidate.offsetC = referenceC - snapshot.temperature.uncalibratedC;
        candidate.calibrated = true;
        const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
        candidate.timestamp = clock.synchronized ? clock.unixSeconds : 0;
        if (!validTemperatureCalibration(candidate)) { server_.send(400, "text/plain", "Offset exceeds 5 C"); return; }
    } else if (action != "reset" || server_.arg("confirm") != "yes") {
        server_.send(400, "text/plain", "Invalid action"); return;
    }
    calibrationWritten_ = true;
    lastCalibrationWriteMs_ = now;
    if (!temperatureCalibrationStore_.save(candidate)) {
        temperatureCalibrationError_ = true;
        server_.send(503, "text/plain", "Calibration storage unavailable"); return;
    }
    temperatureCalibration_ = candidate;
    temperatureOffset_.store(candidate.offsetC);
    temperatureCalibrationError_ = false;
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::serveHistory(bool csv) {
    if (!filesystemReady_) { server_.send(503, "text/plain", "History storage unavailable"); return; }
    const String metric = server_.arg("metric");
    const String range = server_.arg("range");
    HistoryEngine* engine = nullptr;
    float scale = 1.0f;
    if (metric == "temperature") { engine = &temperatureHistory_; scale = 100.0f; }
    else if (metric == "ph") { engine = &phHistory_; scale = 100.0f; }
    else if (metric == "tds") engine = &tdsHistory_;
    std::size_t tier = 0;
    uint32_t duration = 0;
    if (range == "1h") { tier = 0; duration = 3600; }
    else if (range == "24h") { tier = 0; duration = 86400; }
    else if (range == "7d") { tier = 1; duration = 604800; }
    else if (range == "30d") { tier = 2; duration = 2592000; }
    else if (range == "1y") { tier = 3; duration = 31536000; }
    if (!engine || !duration) { server_.send(400, "text/plain", "Invalid metric or range"); return; }
    if (!engine->healthy()) { server_.send(503, "text/plain", "History storage error"); return; }
    const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
    if (!clock.synchronized) {
        server_.sendHeader("Cache-Control", "no-store");
        server_.send(200, csv ? "text/csv" : "application/json", csv ? "timestamp_utc,min,avg,max,count\n" : "{\"synchronized\":false,\"storageError\":false,\"points\":[]}");
        return;
    }
    const uint32_t interval = historyTiers[tier].intervalSeconds;
    const uint32_t count = duration / interval;
    const uint32_t groupSize = (count + 239) / 240;
    const uint32_t latest = clock.unixSeconds - clock.unixSeconds % interval;
    const uint32_t first = latest - (count - 1) * interval;
    server_.sendHeader("Cache-Control", "no-store");
    if (csv) server_.sendHeader("Content-Disposition", "attachment; filename=\"aquamax-history.csv\"");
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, csv ? "text/csv" : "application/json", "");
    server_.sendContent(csv ? "timestamp_utc,min,avg,max,count\n" : "{\"synchronized\":true,\"points\":[");
    bool firstPoint = true;
    bool error = false;
    for (uint32_t group = 0; group < count; group += groupSize) {
        HistoryAggregate aggregate;
        const uint32_t end = group + groupSize < count ? group + groupSize : count;
        for (uint32_t index = group; index < end; ++index) {
            HistoryRecord record;
            const HistoryRead status = engine->read(tier, first + index * interval, record);
            if (status == HistoryRead::error) { error = true; break; }
            if (status == HistoryRead::found) aggregate.add(record);
        }
        if (error) break;
        HistoryPoint point;
        const bool present = aggregate.result(point);
        const uint32_t timestamp = first + group * interval;
        String output;
        if (csv) {
            if (!present) continue;
            output = String(timestamp) + ',' + String(point.minimum / scale, 2) + ',' +
                     String(point.average / scale, 2) + ',' + String(point.maximum / scale, 2) + ',' + String(point.count) + '\n';
        } else {
            if (!firstPoint) output += ',';
            output += '[' + String(timestamp) + ',';
            if (present) output += String(point.minimum / scale, 2) + ',' + String(point.average / scale, 2) + ',' + String(point.maximum / scale, 2) + ',' + String(point.count);
            else output += F("null,null,null,0");
            output += ']';
            firstPoint = false;
        }
        server_.sendContent(output);
        yield();
    }
    if (csv) { if (error) server_.sendContent("# storage_error\n"); }
    else server_.sendContent(error ? "],\"storageError\":true}" : "],\"storageError\":false}");
    server_.sendContent("");
}

void DashboardPreview::update(uint32_t now) {
    const TemperatureSample sample = temperature_.update(now);
    const bool temperatureEnabled = monitoringMask_.load() & monitorTemperature;
    const SensorSnapshot temperatureSnapshot = sensorBuilder_.updateTemperature(temperatureEnabled ? TemperatureReading{sample.state, sample.hasValue, sample.valueC, sample.lastSeenMs} : TemperatureReading{ProbeState::disabled, false, 0.0f, 0}, temperatureOffset_.load());
    portENTER_CRITICAL(&snapshotMutex_);
    snapshot_.temperature = temperatureSnapshot.temperature;
    const SensorSnapshot snapshot = snapshot_;
    portEXIT_CRITICAL(&snapshotMutex_);
#if defined(AQUAMAX_TFT)
    const StartupScreen screen = startup_.update(now, displayWifiConnected_.load(), displayWifiTimedOut_.load());
    if (screen == StartupScreen::splash) {
        showTftSplash(now - startupStartedMs_);
    } else if (screen == StartupScreen::setup) {
        TftSetupInfo setup;
        portENTER_CRITICAL(&setupDisplayMutex_);
        setup = setupDisplay_;
        portEXIT_CRITICAL(&setupDisplayMutex_);
        showTftSetup(setup);
    } else {
        updateTftGauges(snapshot, displayTemperatureAlert_.load(), now);
    }
#endif
    if (now - lastSensorReportMs_ >= 5000) {
        lastSensorReportMs_ = now;
        Serial.print("Sensors: temp_C=");
        if (snapshot.temperature.hasValue) Serial.print(snapshot.temperature.valueC, 2);
        else Serial.print("--");
        Serial.print(" state=");
        Serial.print(stateName(snapshot.temperature.state));
        Serial.print(" pH=");
        Serial.print(stateName(snapshot.ph.state));
        Serial.print(" TDS=");
        Serial.println(stateName(snapshot.tds.state));
    }
}

void DashboardPreview::networkTask(void* context) {
    auto* dashboard = static_cast<DashboardPreview*>(context);
    for (;;) {
        dashboard->updateNetwork(millis());
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#if defined(AQUAMAX_PH_PROBE)
void DashboardPreview::updatePhProbe(uint32_t now) {
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    if (tdsDiagnosticActive_ || tdsDiagnosticRecovering_) return;
#endif
#if defined(AQUAMAX_TDS_PROBE)
    if (!tdsQuiet_) return;
#endif
    if (!(monitoringMask_.load() & monitorPh)) return;
    if (!phWireReady_) {
        portENTER_CRITICAL(&snapshotMutex_);
        snapshot_.ph = {ProbeState::signal_fault, false, 0.0f, 0};
        portEXIT_CRITICAL(&snapshotMutex_);
        return;
    }
    if (phConversionPending_) {
        int16_t raw = 0;
        const AdcResult result = phAdc_.poll(now, raw);
        if (result == AdcResult::ready) {
            phConversionPending_ = false;
            phAdcHasValue_ = true;
            phAdcError_ = false;
            phAdcVolts_ = adsRawToVolts(raw);
            phReading_ = phChannel_.update(phAdcVolts_, phVoltageLimits_, phCalibrationSession_.active() ? PhCalibration{} : phCalibration_, true);
            portENTER_CRITICAL(&snapshotMutex_);
            snapshot_.ph.state = phCalibrationSession_.active() ? ProbeState::waiting : phReading_.state;
            snapshot_.ph.hasValue = phReading_.hasValue && !phCalibrationError_ && !phCalibrationSession_.active();
            snapshot_.ph.value = phReading_.ph;
            snapshot_.ph.lastSeenMs = now;
            snapshot_.ph.calibrated = phCalibration_.valid && !phCalibrationError_;
            snapshot_.ph.calibrationError = phCalibrationError_;
            portEXIT_CRITICAL(&snapshotMutex_);
        } else if (result == AdcResult::error) {
            phConversionPending_ = false;
            phAdcHasValue_ = false;
            phAdcError_ = true;
            phChannel_ = PhChannel{};
            phReading_ = PhReading{};
            portENTER_CRITICAL(&snapshotMutex_);
            snapshot_.ph = {ProbeState::signal_fault, false, 0.0f, 0};
            portEXIT_CRITICAL(&snapshotMutex_);
        }
    } else if (now - lastPhSampleMs_ >= 1000) {
        lastPhSampleMs_ = now;
        phConversionPending_ = phAdc_.start(1, now);
        if (!phConversionPending_) {
            phAdcHasValue_ = false;
            phAdcError_ = true;
            phChannel_ = PhChannel{};
            phReading_ = PhReading{};
            portENTER_CRITICAL(&snapshotMutex_);
            snapshot_.ph = {ProbeState::signal_fault, false, 0.0f, 0};
            portEXIT_CRITICAL(&snapshotMutex_);
        }
    }
}
#endif

#if defined(AQUAMAX_TDS_PROBE)
void DashboardPreview::stopTdsPower(void* context) {
    auto* dashboard = static_cast<DashboardPreview*>(context);
    portENTER_CRITICAL(&dashboard->tdsPowerMutex_);
    digitalWrite(pins::tdsEnable, LOW);
    dashboard->tdsPowerExpired_ = true;
    portEXIT_CRITICAL(&dashboard->tdsPowerMutex_);
}

void DashboardPreview::changeTdsCalibration() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    const String action = server_.arg("action");
    if (action == "cancel") {
        tdsCalibrationActive_ = false;
    } else if (action == "start") {
        if (!(monitoringMask_.load() & monitorTds)) { server_.send(409, "text/plain", "TDS monitoring is disabled"); return; }
        if (tdsCalibrationError_ || !tdsPowerTimer_) { server_.send(503, "text/plain", "TDS calibration unavailable"); return; }
        if (phCalibrationSession_.active() || !tdsQuiet_) { server_.send(409, "text/plain", "Another measurement is in progress"); return; }
        tdsCalibrationActive_ = true;
        tdsHasVoltage_ = false;
        tdsStable_ = false;
        portENTER_CRITICAL(&snapshotMutex_);
        snapshot_.tds.hasValue = false;
        snapshot_.tds.state = ProbeState::waiting;
        portEXIT_CRITICAL(&snapshotMutex_);
    } else if (action == "save") {
        const uint32_t now = millis();
        if (tdsCalibrationError_) { server_.send(503, "text/plain", "TDS calibration storage unavailable"); return; }
        if (!tdsCalibrationActive_ || !tdsQuiet_ || !tdsHasVoltage_ || !tdsStable_ || now - tdsLastSampleMs_ >= 75000) {
            server_.send(409, "text/plain", "Wait for a stable TDS measurement"); return;
        }
        if (tdsCalibrationWritten_ && now - lastTdsCalibrationWriteMs_ < calibrationWriteIntervalMs) {
            server_.send(429, "text/plain", "Try again shortly"); return;
        }
        float reference = 0.0f;
        float factor = 0.0f;
        if (!parseFloat(server_.arg("referencePpm"), reference) || !tdsCalibrationFactor(tdsVoltage_, tdsTemperatureC_, reference, factor)) {
            server_.send(400, "text/plain", "Invalid reference or TDS signal"); return;
        }
        const ClockStatus clock = clockStatus(static_cast<int64_t>(time(nullptr)), networkTimeReceived.load());
        const TdsCalibration candidate{factor, clock.synchronized ? clock.unixSeconds : 0, true};
        tdsCalibrationWritten_ = true;
        lastTdsCalibrationWriteMs_ = now;
        if (!tdsCalibrationStore_.save(candidate)) { server_.send(503, "text/plain", "TDS calibration could not be saved"); return; }
        tdsCalibration_ = candidate;
        tdsCalibrationActive_ = false;
    } else {
        server_.send(400, "text/plain", "Invalid action"); return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", "{\"ok\":true}");
}

void DashboardPreview::updateTdsProbe(uint32_t now) {
    const bool monitored = monitoringMask_.load() & monitorTds;
    const bool enabled = monitored && tdsPowerTimer_ && phWireReady_ && !phCalibrationSession_.active() && !rebootPending_;
    const MeasurementWindow window = tdsScheduler_.update(now, enabled);
    tdsQuiet_ = !enabled || window.samplePh;
    if (!enabled && !tdsPowerOn_) {
        if (!monitored || !tdsPowerTimer_ || !phWireReady_) {
            portENTER_CRITICAL(&snapshotMutex_);
            snapshot_.tds = {monitored ? ProbeState::signal_fault : ProbeState::disabled, false, 0.0f, 0};
            portEXIT_CRITICAL(&snapshotMutex_);
        }
        return;
    }
    portENTER_CRITICAL(&tdsPowerMutex_);
    const bool expired = tdsPowerExpired_;
    portEXIT_CRITICAL(&tdsPowerMutex_);
    if (tdsPowerOn_ && expired) tdsCycleFailed_ = true;
    if (window.tdsPower && !tdsPowerOn_ && !tdsCycleFailed_) {
        phAdc_.cancel();
        phConversionPending_ = false;
        phAdcHasValue_ = false;
        phChannel_ = PhChannel{};
        phReading_ = PhReading{};
        portENTER_CRITICAL(&snapshotMutex_);
        snapshot_.ph = {monitoringMask_.load() & monitorPh ? ProbeState::waiting : ProbeState::disabled, false, 0.0f, 0};
        portEXIT_CRITICAL(&snapshotMutex_);
        tdsSampleCount_ = 0;
        tdsSampleIndex_ = 0;
        tdsPending_ = false;
        portENTER_CRITICAL(&tdsPowerMutex_);
        tdsPowerExpired_ = false;
        portEXIT_CRITICAL(&tdsPowerMutex_);
        if (esp_timer_start_once(tdsPowerTimer_, 5000000) != ESP_OK) {
            tdsCycleFailed_ = true;
            tdsHasVoltage_ = false;
            tdsStable_ = false;
            portENTER_CRITICAL(&snapshotMutex_);
            snapshot_.tds = {ProbeState::signal_fault, false, 0.0f, 0};
            portEXIT_CRITICAL(&snapshotMutex_);
        } else {
            tdsPowerOn_ = true;
            portENTER_CRITICAL(&tdsPowerMutex_);
            if (!tdsPowerExpired_) digitalWrite(pins::tdsEnable, HIGH);
            portEXIT_CRITICAL(&tdsPowerMutex_);
        }
    }
    if (tdsPowerOn_ && (!window.tdsPower || tdsCycleFailed_)) {
        esp_timer_stop(tdsPowerTimer_);
        stopTdsPower(this);
        phAdc_.cancel();
        tdsPending_ = false;
        tdsPowerOn_ = false;
        float voltage = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (uint8_t index = 0; index < tdsSampleCount_; ++index) {
            const float sample = tdsSamples_[index];
            if (index == 0 || sample < minimum) minimum = sample;
            if (index == 0 || sample > maximum) maximum = sample;
            voltage += sample;
        }
        if (tdsSampleCount_) voltage /= tdsSampleCount_;
        if (voltage < 0.0f && voltage >= -0.01f) voltage = 0.0f;
        SensorSnapshot snapshot;
        portENTER_CRITICAL(&snapshotMutex_);
        snapshot = snapshot_;
        portEXIT_CRITICAL(&snapshotMutex_);
        const bool tempFresh = snapshot.temperature.hasValue && now - snapshot.temperature.lastSeenMs < 3000 &&
                               snapshot.temperature.valueC >= -5.0f && snapshot.temperature.valueC <= 60.0f;
        CompensationTemperature temperature;
        resolveCompensationTemperature(tempFresh, snapshot.temperature.valueC, 25.0f, temperature);
        tdsTemperatureC_ = temperature.valueC;
        tdsUsedDefaultTemperature_ = temperature.usedDefault;
        tdsHasVoltage_ = !tdsCycleFailed_ && tdsSampleCount_ == 8 && voltage >= 0.0f && voltage <= 2.3f;
        tdsStable_ = tdsHasVoltage_ && maximum - minimum <= 0.02f;
        tdsVoltage_ = voltage;
        tdsLastSampleMs_ = now;
        float ppm = 0.0f;
        const bool valid = tdsHasVoltage_ && !tdsCalibrationError_ &&
                           tdsEstimate(voltage, temperature.valueC, tdsCalibration_.factor, ppm) && ppm <= 1000.0f;
        portENTER_CRITICAL(&snapshotMutex_);
        const bool aboveSignalRange = !tdsCycleFailed_ && tdsSampleCount_ == 8 && std::isfinite(voltage) && voltage > 2.3f;
        snapshot_.tds.state = tdsCalibrationActive_ ? ProbeState::waiting : aboveSignalRange ? ProbeState::out_of_range : !tdsHasVoltage_ || tdsCalibrationError_ ? ProbeState::signal_fault : !tdsStable_ ? ProbeState::unstable : valid ? ProbeState::connected : ProbeState::out_of_range;
        snapshot_.tds.hasValue = valid && tdsStable_ && !tdsCalibrationActive_;
        snapshot_.tds.value = ppm;
        snapshot_.tds.lastSeenMs = now;
        snapshot_.tds.calibrated = tdsCalibration_.calibrated;
        portEXIT_CRITICAL(&snapshotMutex_);
    }
    if (!window.tdsPower) tdsCycleFailed_ = false;
    if (!window.tdsPower || !window.sampleTds || tdsCycleFailed_ || !tdsPowerOn_) return;
    if (tdsPending_) {
        int16_t raw = 0;
        const AdcResult result = phAdc_.poll(now, raw);
        if (result == AdcResult::ready) {
            tdsPending_ = false;
            tdsSamples_[tdsSampleIndex_] = adsRawToVolts(raw);
            tdsSampleIndex_ = (tdsSampleIndex_ + 1) % 8;
            if (tdsSampleCount_ < 8) ++tdsSampleCount_;
        } else if (result == AdcResult::error) {
            tdsPending_ = false;
            tdsCycleFailed_ = true;
        }
    } else {
        tdsPending_ = phAdc_.start(2, now);
        if (!tdsPending_) tdsCycleFailed_ = true;
    }
}
#endif

#if defined(AQUAMAX_TDS_DIAGNOSTIC)
void DashboardPreview::stopTdsDiagnostic(void* context) {
    auto* dashboard = static_cast<DashboardPreview*>(context);
    portENTER_CRITICAL(&dashboard->tdsDiagnosticMutex_);
    digitalWrite(pins::tdsEnable, LOW);
    dashboard->tdsDiagnosticExpired_ = true;
    portEXIT_CRITICAL(&dashboard->tdsDiagnosticMutex_);
}

void DashboardPreview::changeTdsDiagnostic() {
    if (!allowedOrigin(server_.header("Origin"))) { server_.send(403, "text/plain", "Request rejected"); return; }
    const String action = server_.arg("action");
    if (action == "stop") {
        if (tdsDiagnosticActive_) {
            esp_timer_stop(tdsDiagnosticTimer_);
            stopTdsDiagnostic(this);
        }
        server_.send(200, "application/json", "{\"ok\":true}");
        return;
    }
    if (action != "start" || server_.arg("confirm") != "yes") { server_.send(400, "text/plain", "Invalid action"); return; }
    if (!tdsDiagnosticTimer_ || tdsDiagnosticActive_ || tdsDiagnosticRecovering_ || phCalibrationSession_.active() || rebootPending_) {
        server_.send(409, "text/plain", "Diagnostic unavailable"); return;
    }
    phAdc_.cancel();
    phConversionPending_ = false;
    phAdcHasValue_ = false;
    phChannel_ = PhChannel{};
    phReading_ = PhReading{};
    portENTER_CRITICAL(&snapshotMutex_);
    snapshot_.ph = {ProbeState::disabled, false, 0.0f, 0};
    portEXIT_CRITICAL(&snapshotMutex_);
    portENTER_CRITICAL(&tdsDiagnosticMutex_);
    tdsDiagnosticExpired_ = false;
    portEXIT_CRITICAL(&tdsDiagnosticMutex_);
    tdsDiagnosticPending_ = false;
    tdsDiagnosticHasValue_ = false;
    tdsDiagnosticAdcError_ = false;
    tdsDiagnosticSampleMs_ = millis();
    tdsDiagnosticActive_ = true;
    if (esp_timer_start_once(tdsDiagnosticTimer_, 30000000) != ESP_OK) {
        tdsDiagnosticActive_ = false;
        server_.send(503, "text/plain", "Timer unavailable"); return;
    }
    portENTER_CRITICAL(&tdsDiagnosticMutex_);
    if (!tdsDiagnosticExpired_) digitalWrite(pins::tdsEnable, HIGH);
    portEXIT_CRITICAL(&tdsDiagnosticMutex_);
    server_.send(200, "application/json", "{\"ok\":true,\"seconds\":30}");
}

void DashboardPreview::updateTdsDiagnostic(uint32_t now) {
    portENTER_CRITICAL(&tdsDiagnosticMutex_);
    const bool expired = tdsDiagnosticExpired_;
    portEXIT_CRITICAL(&tdsDiagnosticMutex_);
    if (tdsDiagnosticActive_ && expired) {
        phAdc_.cancel();
        tdsDiagnosticPending_ = false;
        tdsDiagnosticActive_ = false;
        tdsDiagnosticRecovering_ = true;
        tdsDiagnosticStoppedMs_ = now;
    } else if (tdsDiagnosticRecovering_ && now - tdsDiagnosticStoppedMs_ >= 2000) {
        tdsDiagnosticRecovering_ = false;
    } else if (tdsDiagnosticActive_ && tdsDiagnosticPending_) {
        int16_t raw = 0;
        const AdcResult result = phAdc_.poll(now, raw);
        if (result == AdcResult::ready) {
            tdsDiagnosticPending_ = false;
            tdsDiagnosticHasValue_ = true;
            tdsDiagnosticAdcError_ = false;
            tdsDiagnosticVolts_ = adsRawToVolts(raw);
        } else if (result == AdcResult::error) {
            tdsDiagnosticPending_ = false;
            tdsDiagnosticHasValue_ = false;
            tdsDiagnosticAdcError_ = true;
        }
    } else if (tdsDiagnosticActive_ && now - tdsDiagnosticSampleMs_ >= 1000) {
        tdsDiagnosticSampleMs_ = now;
        tdsDiagnosticPending_ = phAdc_.start(2, now);
        if (!tdsDiagnosticPending_) {
            tdsDiagnosticHasValue_ = false;
            tdsDiagnosticAdcError_ = true;
        }
    }
}
#endif

void DashboardPreview::updateNetwork(uint32_t now) {
#if defined(AQUAMAX_TDS_PROBE)
    updateTdsProbe(now);
#endif
#if defined(AQUAMAX_TDS_DIAGNOSTIC)
    updateTdsDiagnostic(now);
#endif
#if defined(AQUAMAX_PH_PROBE)
    updatePhProbe(now);
#endif
    if (setupActive_) dns_.processNextRequest();
    server_.handleClient();
    socket_.loop();
    now = millis();
    if (rebootPending_ && now - rebootRequestedMs_ >= responseGraceMs) ESP.restart();
    updateHistory(now);
    updateAlerts(now);
    if (now - lastBroadcastMs_ >= broadcastIntervalMs) {
        lastBroadcastMs_ = now;
        if (socket_.connectedClients() > 0) {
            String state = stateJson();
            socket_.broadcastTXT(state);
        }
    }
    if (connectQueued_ && now - connectRequested_ >= responseGraceMs && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        connectQueued_ = false;
        WiFi.scanDelete();
        WiFi.disconnect(false, false);
        WiFi.setAutoReconnect(true);
        const wl_status_t startStatus = WiFi.begin(pendingSsid_.c_str(), pendingPassword_.c_str());
        attemptStarted_ = now;
        attempting_ = true;
        connected_ = false;
        Serial.println("Connecting to requested Wi-Fi network...");
        if (startStatus == WL_CONNECT_FAILED) Serial.println("Wi-Fi could not start the connection attempt.");
    }
    const int reason = disconnectReason_.exchange(0);
    if (reason != 0 && attempting_) {
        lastDisconnectReason_ = reason;
        Serial.printf("Wi-Fi disconnect reason: %d (%s)\n", reason,
                      WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)));
    }
    const bool online = WiFi.status() == WL_CONNECTED &&
                        (pendingSsid_.length() == 0 || WiFi.SSID() == pendingSsid_);
    if (online && !connected_) {
        connected_ = true;
        connectedSince_ = now;
        attempting_ = false;
        if (pendingSsid_.length()) {
            WifiCredentials credentials{};
            pendingSsid_.toCharArray(credentials.ssid, sizeof(credentials.ssid));
            pendingPassword_.toCharArray(credentials.password, sizeof(credentials.password));
            Preferences storage;
            if (!storage.begin("wifi-preview", false) || storage.putBytes("network", &credentials, sizeof(credentials)) != sizeof(credentials)) {
                Serial.println("Warning: could not save Wi-Fi credentials.");
            }
            storage.end();
            pendingSsid_ = "";
            pendingPassword_ = "";
        }
        if (!mdnsStarted_) mdnsStarted_ = MDNS.begin("aquamax");
        if (!mdnsStarted_) Serial.println("mDNS unavailable; use the IP address.");
        configTzTime(timezone_->rule, "pool.ntp.org", "time.cloudflare.com");
        Serial.print("Dashboard: http://");
        Serial.println(WiFi.localIP());
        Serial.println("Also try http://aquamax.local");
    } else if (!online && connected_) {
        connected_ = false;
        attempting_ = true;
        attemptStarted_ = now;
        if (!setupActive_ && mdnsStarted_) { MDNS.end(); mdnsStarted_ = false; }
        Serial.println("Wi-Fi disconnected; reconnecting.");
    }
#if defined(AQUAMAX_TFT)
    displayWifiConnected_.store(online);
#endif
    if (!online && attempting_ && now - attemptStarted_ >= connectionTimeoutMs) {
        attempting_ = false;
#if defined(AQUAMAX_TFT)
        displayWifiTimedOut_.store(true);
#endif
        const wl_status_t timeoutStatus = WiFi.status();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        pendingSsid_ = "";
        pendingPassword_ = "";
        setupFailed_ = true;
        Serial.printf("Wi-Fi connection timed out (status %d, last reason %d); reconnect to setup to retry.\n",
                      static_cast<int>(timeoutStatus), lastDisconnectReason_);
        startSetup();
    }
    if (online && setupActive_ && now - connectedSince_ >= setupGraceMs) {
        WiFi.scanDelete();
        dns_.stop();
        WiFi.softAPdisconnect(true);
        setupActive_ = false;
        setupToken_ = "";
    #if defined(AQUAMAX_TFT)
        portENTER_CRITICAL(&setupDisplayMutex_);
        setupDisplay_ = {};
        portEXIT_CRITICAL(&setupDisplayMutex_);
    #endif
    }
}

} // namespace aquamax
#endif