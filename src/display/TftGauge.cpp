#if defined(AQUAMAX_TFT)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "config/pins.h"
#include "display/GaugeModel.h"
#include "display/StartupModel.h"
#include "display/TftGauge.h"
#include "generated/icons.h"

namespace aquamax {
namespace {
constexpr int screenSize = 240;
constexpr int center = screenSize / 2;
constexpr float pi = 3.14159265358979323846f;
constexpr uint32_t frameIntervalMs = 200;
constexpr uint32_t splashFrameIntervalMs = 80;
constexpr uint16_t backgroundColor = 0x0841;
constexpr uint16_t mutedColor = 0x8410;
constexpr uint16_t dimColor = 0x39e7;
constexpr uint16_t textColor = 0xef7d;
constexpr uint16_t warningColor = 0xf9a8;
constexpr uint16_t temperatureColor = 0x5e9f;

class SharedSpiBus : public lgfx::Bus_SPI {
public:
	bool init() override {
		if (!initialized_) initialized_ = lgfx::Bus_SPI::init();
		return initialized_;
	}

	void release() override {
		lgfx::Bus_SPI::release();
		initialized_ = false;
	}

private:
	bool initialized_ = false;
};

class DisplaySet {
public:
	DisplaySet() : base_(&displays_[0]), canvas_(&displays_[0]), logo_(&displays_[1]), temperature_({0.0f, 40.0f}) {
		auto busConfig = bus_.config();
		busConfig.spi_host = SPI2_HOST;
		busConfig.spi_mode = 0;
		busConfig.freq_write = 4000000;
		busConfig.freq_read = 10000000;
		busConfig.spi_3wire = false;
		busConfig.use_lock = true;
		busConfig.dma_channel = SPI_DMA_CH_AUTO;
		busConfig.pin_sclk = pins::tftSclk;
		busConfig.pin_mosi = pins::tftMosi;
		busConfig.pin_miso = -1;
		busConfig.pin_dc = pins::tftDc;
		bus_.config(busConfig);
		const int selectPins[] = {pins::tftTemperatureCs, pins::tftPhCs, pins::tftTdsCs};
		for (int index = 0; index < 3; ++index) {
			panels_[index].setBus(&bus_);
			auto panelConfig = panels_[index].config();
			panelConfig.pin_cs = selectPins[index];
			panelConfig.pin_rst = -1;
			panelConfig.panel_width = screenSize;
			panelConfig.panel_height = screenSize;
			panelConfig.memory_width = screenSize;
			panelConfig.memory_height = screenSize;
			panelConfig.readable = false;
			panelConfig.bus_shared = true;
			panelConfig.invert = true;
			panels_[index].config(panelConfig);
			displays_[index].setPanel(&panels_[index]);
		}
	}

	bool begin() {
		const int selectPins[] = {pins::tftTemperatureCs, pins::tftPhCs, pins::tftTdsCs};
		for (int pin : selectPins) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
		pinMode(pins::tftReset, OUTPUT);
		digitalWrite(pins::tftReset, LOW);
		delay(20);
		digitalWrite(pins::tftReset, HIGH);
		delay(120);
		for (auto& display : displays_) {
			if (!display.init()) return false;
			display.fillScreen(0);
		}
		base_.setPsram(true);
		canvas_.setPsram(true);
		logo_.setPsram(true);
		base_.setColorDepth(16);
		canvas_.setColorDepth(16);
		logo_.setColorDepth(16);
		if (!base_.createSprite(screenSize, screenSize) || !canvas_.createSprite(screenSize, screenSize) ||
			!logo_.createSprite(176, 43)) return false;
		return true;
	}

	void splash(uint32_t elapsedMs) {
		if (mode_ != Mode::splash) {
			previousSetup_ = {};
			displays_[1].fillScreen(0);
			mode_ = Mode::splash;
			splashDrawn_ = false;
		}
		if (splashDrawn_ && elapsedMs - lastSplashMs_ < splashFrameIntervalMs) return;
		lastSplashMs_ = elapsedMs;
		const float phase = static_cast<float>(elapsedMs % StartupModel::animationCycleMs) / StartupModel::animationCycleMs;
		const uint8_t brightness = static_cast<uint8_t>((1.0f - std::cos(2.0f * pi * phase)) * 127.5f);
		for (int row = 0; row < 43; ++row) for (int column = 0; column < 176; ++column) {
			const uint8_t value = static_cast<uint8_t>(icons::logo_176_43[row * 176 + column] * brightness / 255);
			logo_.drawPixel(column, row, lgfx::color565(value, value, value));
		}
		logo_.pushSprite(&displays_[1], (screenSize - 176) / 2, (screenSize - 43) / 2);
		splashDrawn_ = true;
	}

	void setup(const TftSetupInfo& setup) {
		if (mode_ == Mode::setup && setup.active == previousSetup_.active && setup.failed == previousSetup_.failed &&
			std::strcmp(setup.ssid, previousSetup_.ssid) == 0 && std::strcmp(setup.password, previousSetup_.password) == 0) return;
		mode_ = Mode::setup;
		previousSetup_ = setup;
		canvas_.fillSprite(0);
		canvas_.setTextDatum(textdatum_t::middle_center);
		canvas_.setTextSize(1);
		canvas_.setTextColor(0xffff, 0);
		canvas_.setFont(&fonts::Font2);
		canvas_.drawString("WI-FI SETUP", center, 53);
		if (setup.active) {
			canvas_.setFont(&fonts::Font0);
			canvas_.drawString("NETWORK", center, 75);
			canvas_.setFont(&fonts::Font2);
			canvas_.drawString(setup.ssid, center, 92);
			canvas_.setFont(&fonts::Font0);
			canvas_.drawString("PASSWORD", center, 114);
			canvas_.setFont(&fonts::Font2);
			canvas_.drawString(setup.password, center, 131);
			canvas_.setFont(&fonts::Font0);
			canvas_.drawString("PORTAL", center, 153);
			canvas_.drawString("http://192.168.4.1/setup", center, 169);
		} else {
			canvas_.setFont(&fonts::Font2);
			canvas_.drawString(setup.failed ? "Setup AP failed" : "Starting setup...", center, 110);
			if (setup.failed) canvas_.drawString("Restart device", center, 132);
		}
		canvas_.pushSprite(&displays_[1], 0, 0);
	}

	void update(const SensorSnapshot& snapshot, bool alert, uint32_t nowMs) {
		if (mode_ != Mode::gauges) {
			previousSetup_ = {};
			mode_ = Mode::gauges;
			drawn_ = false;
			drawStatic(false, false, icons::drop_off_32, "pH", "pH");
			drawValue({}, "Not enabled");
			canvas_.pushSprite(&displays_[1], 0, 0);
			drawStatic(false, false, icons::three_drops_off_32, "ppm", "TDS");
			drawValue({}, "Not enabled");
			canvas_.pushSprite(&displays_[2], 0, 0);
		}
		if (drawn_ && nowMs - lastFrameMs_ < frameIntervalMs) return;
		lastFrameMs_ = nowMs;
		const bool fresh = snapshot.temperature.hasValue && nowMs - snapshot.temperature.lastSeenMs < 3000;
		const GaugeFrame frame = temperature_.update(snapshot.temperature.state, fresh, snapshot.temperature.valueC, alert, nowMs);
		if (!drawn_ || frame.hasValue != previousValue_ || frame.alert != previousAlert_) {
			drawStatic(frame.hasValue, frame.alert, frame.hasValue ? icons::thermometer_32 : icons::thermometer_off_32, "C", "TEMPERATURE");
			previousValue_ = frame.hasValue;
			previousAlert_ = frame.alert;
		}
		const char* status = frame.state == ProbeState::connected ? "Waiting" : frame.state == ProbeState::disconnected ? "Disconnected" : frame.state == ProbeState::disabled ? "Not enabled" : "Probe error";
		drawValue(frame, status);
		canvas_.pushSprite(&displays_[0], 0, 0);
		drawn_ = true;
	}

private:
	enum class Mode { none, splash, setup, gauges };
	static int pointX(float position, int radius) { return center + static_cast<int>(std::cos((135.0f + 270.0f * position) * pi / 180.0f) * radius); }
	static int pointY(float position, int radius) { return center + static_cast<int>(std::sin((135.0f + 270.0f * position) * pi / 180.0f) * radius); }

	static uint16_t gradient(float position) {
		constexpr uint8_t stops[][3] = {{47, 126, 223}, {61, 207, 220}, {81, 194, 130}, {239, 177, 69}, {219, 72, 66}};
		const float scaled = position * 4.0f;
		const int index = scaled >= 4.0f ? 3 : static_cast<int>(scaled);
		const float blend = scaled - index;
		const uint8_t red = static_cast<uint8_t>(stops[index][0] + (stops[index + 1][0] - stops[index][0]) * blend);
		const uint8_t green = static_cast<uint8_t>(stops[index][1] + (stops[index + 1][1] - stops[index][1]) * blend);
		const uint8_t blue = static_cast<uint8_t>(stops[index][2] + (stops[index + 1][2] - stops[index][2]) * blend);
		return lgfx::color565(red, green, blue);
	}

	void icon(const uint8_t* mask, int left, int top, uint8_t red, uint8_t green, uint8_t blue) {
		for (int row = 0; row < 32; ++row) for (int column = 0; column < 32; ++column) {
			const uint8_t alpha = mask[row * 32 + column];
			if (alpha) base_.drawPixel(left + column, top + row, lgfx::color565((red * alpha + 8 * (255 - alpha)) / 255, (green * alpha + 8 * (255 - alpha)) / 255, (blue * alpha + 8 * (255 - alpha)) / 255));
		}
	}

	void drawStatic(bool active, bool alert, const uint8_t* mask, const char* unit, const char* label) {
		base_.fillSprite(backgroundColor);
		for (int segment = 0; segment < 270; ++segment) {
			const float start = static_cast<float>(segment) / 270.0f;
			const float end = static_cast<float>(segment + 1) / 270.0f;
			const uint16_t color = active ? gradient(start) : mutedColor;
			for (int radius = 95; radius <= 99; ++radius) base_.drawLine(pointX(start, radius), pointY(start, radius), pointX(end, radius), pointY(end, radius), color);
			base_.drawPixel(pointX(start, 104), pointY(start, 104), alert ? warningColor : dimColor);
		}
		for (int tick = 0; tick <= 10; ++tick) {
			const float position = static_cast<float>(tick) / 10.0f;
			base_.drawLine(pointX(position, 86), pointY(position, 86), pointX(position, 90), pointY(position, 90), dimColor);
		}
		if (active) icon(mask, 104, 51, 101, 211, 245);
		else icon(mask, 104, 51, 160, 170, 170);
		base_.setTextDatum(textdatum_t::middle_center);
		base_.setTextColor(active ? textColor : mutedColor, backgroundColor);
		base_.setTextSize(1);
		base_.setFont(&fonts::Font2);
		base_.drawString(unit, center, 150);
		base_.drawString(label, center, 172);
		if (!active) {
			for (int row = 0; row < 32; ++row) for (int column = 0; column < 32; ++column) {
				const uint8_t alpha = icons::warning_32[row * 32 + column];
				if (alpha) base_.drawPixel(104 + column, 186 + row, lgfx::color565((240 * alpha + 8 * (255 - alpha)) / 255, (60 * alpha + 8 * (255 - alpha)) / 255, (48 * alpha + 8 * (255 - alpha)) / 255));
			}
		}
	}

	void drawValue(const GaugeFrame& frame, const char* status) {
		base_.pushSprite(&canvas_, 0, 0);
		canvas_.setTextDatum(textdatum_t::middle_center);
		canvas_.setFont(&fonts::Font4);
		canvas_.setTextSize(2);
		canvas_.setTextColor(frame.hasValue ? (frame.alert ? warningColor : temperatureColor) : mutedColor, backgroundColor);
		char value[20] = {};
		if (frame.hasValue) std::snprintf(value, sizeof(value), "%.1f", frame.value);
		else std::snprintf(value, sizeof(value), "--");
		canvas_.drawString(value, center, 111);
		canvas_.setTextSize(1);
		if (frame.showCursor) {
			canvas_.fillCircle(pointX(frame.cursorPosition, 97), pointY(frame.cursorPosition, 97), 4, textColor);
			canvas_.fillCircle(center, 202, 3, frame.alert ? warningColor : temperatureColor);
		} else {
			canvas_.setFont(&fonts::Font0);
			canvas_.setTextColor(mutedColor, backgroundColor);
			canvas_.drawString(status, center, 229);
		}
	}

	SharedSpiBus bus_;
	lgfx::Panel_GC9A01 panels_[3];
	lgfx::LGFX_Device displays_[3];
	lgfx::LGFX_Sprite base_;
	lgfx::LGFX_Sprite canvas_;
	lgfx::LGFX_Sprite logo_;
	GaugeModel temperature_;
	TftSetupInfo previousSetup_;
	Mode mode_ = Mode::none;
	uint32_t lastSplashMs_ = 0;
	uint32_t lastFrameMs_ = 0;
	bool splashDrawn_ = false;
	bool drawn_ = false;
	bool previousValue_ = false;
	bool previousAlert_ = false;
};

DisplaySet displays;
bool ready = false;
}

bool beginTftGauges() { ready = displays.begin(); return ready; }
void showTftSplash(uint32_t elapsedMs) { if (ready) displays.splash(elapsedMs); }
void showTftSetup(const TftSetupInfo& setup) { if (ready) displays.setup(setup); }
void updateTftGauges(const SensorSnapshot& snapshot, bool temperatureAlert, uint32_t nowMs) {
	if (ready) displays.update(snapshot, temperatureAlert, nowMs);
}
}
#endif