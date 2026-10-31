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
#include <cstring>
#include <time.h>
#include <unistd.h>
#include "config/pins.h"
#include "sensors/ClockStatus.h"
#include "sensors/HistoryAggregate.h"
#include "display/TftGauge.h"

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
    server_.on("/", HTTP_GET, [this] { serveAsset("/index.html", "text/html"); });
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
    server_.on("/api/history", HTTP_GET, [this] { serveHistory(false); });
    server_.on("/api/history/export.csv", HTTP_GET, [this] { serveHistory(true); });
    server_.on("/api/history/status", HTTP_GET, [this] { serveHistoryStatus(); });
    server_.on("/api/history", HTTP_DELETE, [this] { clearHistory(); });
    server_.on("/api/calibration", HTTP_GET, [this] { serveCalibration(); });
    server_.on("/api/calibration/temp", HTTP_POST, [this] { changeTemperatureCalibration(); });
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
    Serial.printf("Setup Wi-Fi: %s\nSetup password: %s\nOpen http://192.168.4.1/setup\n", name, password);
}

bool DashboardPreview::onSetupNetwork() {
    return setupActive_ && server_.client().localIP() == WiFi.softAPIP();
}

void DashboardPreview::serveSetup() {
    if (!onSetupNetwork()) {
        server_.send(404, "text/plain", "Not found");
        return;
    }
    String page = F("<!doctype html><html lang='en'><meta name='viewport' content='width=device-width,initial-scale=1'><title>AquaMax Wi-Fi setup</title><body><main><h1>AquaMax Wi-Fi setup</h1><p>Credentials are sent without encryption on the setup network.</p><form method='post' action='/setup'><input type='hidden' name='token' value='");
    page += setupToken_;
    page += F("'><label>Wi-Fi network name <input name='ssid' maxlength='32' required autocomplete='off'></label><br><label>Wi-Fi password <input name='password' type='password' maxlength='63' autocomplete='off'></label><br><button type='submit'>Connect</button></form></main></body></html>");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "text/html", page);
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
    response += F("\",\"value\":null},\"tds\":{\"state\":\"");
    response += stateName(snapshot.tds.state);
    response += F("\",\"value\":null},\"wifi\":\"");
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
    response += F(",\"ph\":\"disabled\",\"tds\":\"disabled\"}");
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
            !phHistory_.sample(clock.unixSeconds, false, 0) ||
            !tdsHistory_.sample(clock.unixSeconds, false, 0)) {
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
    response += F("},\"ph\":{\"state\":\"disabled\"},\"tds\":{\"state\":\"disabled\"}}");
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", response);
}

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
    const SensorSnapshot snapshot = sensorBuilder_.updateTemperature({sample.state, sample.hasValue, sample.valueC, sample.lastSeenMs}, temperatureOffset_.load());
    portENTER_CRITICAL(&snapshotMutex_);
    snapshot_ = snapshot;
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

void DashboardPreview::updateNetwork(uint32_t now) {
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
    if (connectQueued_ && now - connectRequested_ >= responseGraceMs) {
        connectQueued_ = false;
        WiFi.disconnect(false, false);
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
        if (!MDNS.begin("aquamax")) Serial.println("mDNS unavailable; use the IP address.");
        configTzTime(timezone_->rule, "pool.ntp.org", "time.cloudflare.com");
        Serial.print("Dashboard: http://");
        Serial.println(WiFi.localIP());
        Serial.println("Also try http://aquamax.local");
    } else if (!online && connected_) {
        connected_ = false;
        attempting_ = true;
        attemptStarted_ = now;
        MDNS.end();
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
        if (pendingSsid_.length()) WiFi.disconnect(false, false);
        pendingSsid_ = "";
        pendingPassword_ = "";
        setupFailed_ = true;
        Serial.printf("Wi-Fi connection timed out (status %d, last reason %d); reconnect to setup to retry.\n",
                      static_cast<int>(timeoutStatus), lastDisconnectReason_);
        startSetup();
    }
    if (online && setupActive_ && now - connectedSince_ >= setupGraceMs) {
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