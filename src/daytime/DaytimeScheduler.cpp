// SPDX-License-Identifier: GPL-3.0-or-later
#include "daytime/DaytimeScheduler.h"

#include <LittleFS.h>
#include <Logger.h>
#include <array>
#include <ctime>

#include "config/ConfigManager.h"
#include "display/ScreenManager.h"
#include "weather/WeatherLayer.h"

extern ConfigManager configManager;

namespace {

constexpr const char* TAG = "Daytime";
constexpr unsigned long CHECK_INTERVAL_MS = 15000UL;
constexpr time_t EPOCH_SANE = 1577836800;  // 2020-01-01, anything earlier is the power-on default

// localtime minus UTC in minutes, including daylight saving as the TZ rule sees it
auto utcOffsetMinutes(time_t now, const tm& local) -> int32_t {
    tm utc{};
    gmtime_r(&now, &utc);
    int32_t diff = (local.tm_hour * 60 + local.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (local.tm_yday != utc.tm_yday) {
        diff += (local.tm_yday > utc.tm_yday || (local.tm_yday == 0 && utc.tm_yday > 1)) ? 1440 : -1440;
    }
    return diff;
}

}  // namespace

AnimationScreen* DaytimeScheduler::_screen = nullptr;
bool DaytimeScheduler::_enabled = true;
bool DaytimeScheduler::_applied = false;
SunPhase::Phase DaytimeScheduler::_phase = SunPhase::Phase::Day;
SunPhase::Times DaytimeScheduler::_times{};
int DaytimeScheduler::_timesForDay = -1;
unsigned long DaytimeScheduler::_nextCheckMs = 0;
unsigned long DaytimeScheduler::_demoMs = 0;
uint8_t DaytimeScheduler::_demoStep = 0;

auto DaytimeScheduler::begin(AnimationScreen* screen) -> void {
    _screen = screen;
    _enabled = configManager.getDaytimeMode();
    _applied = false;
    _nextCheckMs = 0;
    loop();  // pick the first scene now, so the screen never opens the fallback file
}

auto DaytimeScheduler::timeSynced() -> bool { return time(nullptr) > EPOCH_SANE; }

auto DaytimeScheduler::phase() -> SunPhase::Phase { return _phase; }

auto DaytimeScheduler::times() -> const SunPhase::Times& { return _times; }

auto DaytimeScheduler::enabled() -> bool { return _enabled; }

auto DaytimeScheduler::setEnabled(bool enabled) -> void {
    _enabled = enabled;
    _applied = false;  // re-apply the current phase on the next check
    _nextCheckMs = 0;
}

auto DaytimeScheduler::fileFor(SunPhase::Phase phase) -> String {
    const char* file = nullptr;
    switch (phase) {
        case SunPhase::Phase::Night:
            file = configManager.getSceneNight();
            break;
        case SunPhase::Phase::Morning:
            file = configManager.getSceneMorning();
            break;
        case SunPhase::Phase::Evening:
            file = configManager.getSceneEvening();
            break;
        default:
            file = configManager.getSceneDay();
            break;
    }
    return String("/gif/") + file;
}

auto DaytimeScheduler::apply(SunPhase::Phase phase) -> void {
    const String path = fileFor(phase);
    if (!LittleFS.exists(path)) {
        Logger::warn((String("Scene file missing: ") + path).c_str(), TAG);
        return;
    }
    if (_screen->file() != path || !_applied) {
        _screen->setFile(path);
        ScreenManager::invalidate();
        Logger::info((String("Scene ") + SunPhase::name(phase) + " -> " + path).c_str(), TAG);
    }
    _phase = phase;
    _applied = true;
    _screen->setNight(phase == SunPhase::Phase::Night);
}

auto DaytimeScheduler::setDemo(unsigned long ms) -> void {
    _demoMs = ms;
    _demoStep = 0;
    _applied = false;
    _nextCheckMs = 0;
    if (ms == 0) {
        WeatherLayer::setOverride(false);
    }
    Logger::info(ms != 0 ? "Demo cycle on" : "Demo cycle off", TAG);
}

auto DaytimeScheduler::demoIntervalMs() -> unsigned long { return _demoMs; }

auto DaytimeScheduler::loop() -> void {
    if (_screen == nullptr || millis() < _nextCheckMs) {
        return;
    }

    if (_demoMs != 0) {
        // scene and weather combinations that show every prop at least once
        struct Combo {
            SunPhase::Phase phase;
            Weather::Kind kind;
            uint8_t level;
        };
        static constexpr std::array<Combo, 8> DEMO = {{
            {SunPhase::Phase::Morning, Weather::Kind::Clear, 1},
            {SunPhase::Phase::Day, Weather::Kind::Cloudy, 2},
            {SunPhase::Phase::Evening, Weather::Kind::Rain, 2},
            {SunPhase::Phase::Night, Weather::Kind::Clear, 1},
            {SunPhase::Phase::Day, Weather::Kind::Fog, 2},
            {SunPhase::Phase::Morning, Weather::Kind::Snow, 2},
            {SunPhase::Phase::Night, Weather::Kind::Thunder, 3},
            {SunPhase::Phase::Evening, Weather::Kind::Snow, 1},
        }};
        _nextCheckMs = millis() + _demoMs;
        const Combo& c = DEMO.at(_demoStep);
        _demoStep = static_cast<uint8_t>((_demoStep + 1) % DEMO.size());
        WeatherLayer::setOverride(true, {c.kind, c.level});
        _applied = false;  // force the file swap even when the phase repeats
        apply(c.phase);
        return;
    }

    if (!_enabled) {
        return;
    }
    _nextCheckMs = millis() + CHECK_INTERVAL_MS;

    if (!timeSynced()) {
        if (!_applied) {
            apply(SunPhase::Phase::Day);
        }
        return;
    }

    const time_t now = time(nullptr);
    tm local{};
    localtime_r(&now, &local);

    if (local.tm_yday != _timesForDay) {
        _times = SunPhase::compute(configManager.getLatitude(), configManager.getLongitude(), local,
                                   utcOffsetMinutes(now, local));
        _timesForDay = local.tm_yday;
    }

    const SunPhase::Phase next = SunPhase::phaseAt(_times, static_cast<int16_t>(local.tm_hour * 60 + local.tm_min));
    if (next != _phase || !_applied) {
        apply(next);
    }
}
