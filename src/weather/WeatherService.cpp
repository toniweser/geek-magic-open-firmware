// SPDX-License-Identifier: GPL-3.0-or-later
#include "weather/WeatherService.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <Logger.h>
#include <array>
#include <cmath>

#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;

namespace {

constexpr const char* TAG = "Weather";
constexpr unsigned long FIRST_ATTEMPT_DELAY_MS = 5000UL;
constexpr unsigned long REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint16_t HTTP_TIMEOUT_MS = 4000;
constexpr size_t URL_BUF_SIZE = 256;

}  // namespace

WeatherData WeatherService::_data;
unsigned long WeatherService::_nextAttemptMs = FIRST_ATTEMPT_DELAY_MS;
unsigned long WeatherService::_lastAttemptMs = 0;
String WeatherService::_lastStatus = "not fetched yet";

auto WeatherService::begin() -> void { _nextAttemptMs = millis() + FIRST_ATTEMPT_DELAY_MS; }

auto WeatherService::loop() -> void {
    if (millis() < _nextAttemptMs) {
        return;
    }
    if (!WiFiManager::isConnected() || !configManager.getWeatherMode()) {
        _nextAttemptMs = millis() + RETRY_INTERVAL_MS;
        return;
    }
    _nextAttemptMs = millis() + (fetchNow() ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS);
}

auto WeatherService::fetchNow() -> bool {
    _lastAttemptMs = millis();

    std::array<char, URL_BUF_SIZE> url{};
    snprintf(url.data(), url.size(),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,is_day&timezone=auto&forecast_days=1",
             static_cast<double>(configManager.getLatitude()),
             static_cast<double>(configManager.getLongitude()));

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);

    if (!http.begin(client, url.data())) {
        _lastStatus = "http.begin failed";
        Logger::warn(_lastStatus.c_str(), TAG);
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        _lastStatus = String("HTTP ") + code;
        http.end();
        Logger::warn(_lastStatus.c_str(), TAG);
        return false;
    }

    // Only the handful of fields we draw survive the filter, the rest never hits the heap
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"] = true;
    filter["current"]["is_day"] = true;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        _lastStatus = String("JSON: ") + err.c_str();
        Logger::warn(_lastStatus.c_str(), TAG);
        return false;
    }

    JsonObject current = doc["current"];
    if (current.isNull()) {
        _lastStatus = "unexpected payload";
        Logger::warn(_lastStatus.c_str(), TAG);
        return false;
    }

    _data.tempCurrent = current["temperature_2m"] | 0.0F;
    _data.wmoCode = current["weather_code"] | 0;
    _data.isDay = (current["is_day"] | 1) == 1;
    _data.valid = true;
    _data.revision++;

    std::array<char, 96> msg{};
    snprintf(msg.data(), msg.size(), "now %.1f C, code %u, %s", static_cast<double>(_data.tempCurrent), _data.wmoCode,
             _data.isDay ? "day" : "night");
    _lastStatus = msg.data();
    Logger::info(msg.data(), TAG);

    return true;
}

auto WeatherService::data() -> const WeatherData& { return _data; }

auto WeatherService::lastAttemptMs() -> unsigned long { return _lastAttemptMs; }

auto WeatherService::lastStatus() -> const char* { return _lastStatus.c_str(); }
