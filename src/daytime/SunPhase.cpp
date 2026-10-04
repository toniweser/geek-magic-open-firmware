// SPDX-License-Identifier: GPL-3.0-or-later
#include "daytime/SunPhase.h"

#include <cmath>

namespace {

constexpr float DEG = static_cast<float>(M_PI) / 180.0F;
constexpr float ZENITH_SUNRISE = 90.833F;  // refraction and solar disc
constexpr float ZENITH_CIVIL = 96.0F;
constexpr int16_t MORNING_AFTER_SUNRISE_MIN = 120;
constexpr int16_t EVENING_BEFORE_SUNSET_MIN = 120;

struct Solar {
    float eqTime;  // minutes
    float decl;    // radians
};

auto solarForDay(int dayOfYear) -> Solar {
    const float g = (360.0F / 365.25F) * (static_cast<float>(dayOfYear) + 0.5F) * DEG;
    Solar s{};
    s.eqTime = 229.18F * (0.000075F + 0.001868F * cosf(g) - 0.032077F * sinf(g) - 0.014615F * cosf(2 * g) -
                          0.040849F * sinf(2 * g));
    s.decl = 0.006918F - 0.399912F * cosf(g) + 0.070257F * sinf(g) - 0.006758F * cosf(2 * g) +
             0.000907F * sinf(2 * g) - 0.002697F * cosf(3 * g) + 0.00148F * sinf(3 * g);
    return s;
}

// Hour angle in degrees for a zenith, or NaN when the sun never reaches it that day
auto hourAngle(float zenith, float latRad, float decl) -> float {
    const float c = cosf(zenith * DEG) / (cosf(latRad) * cosf(decl)) - tanf(latRad) * tanf(decl);
    if (c < -1.0F || c > 1.0F) {
        return NAN;
    }
    return acosf(c) / DEG;
}

auto toMinutes(float v) -> int16_t {
    while (v < 0) {
        v += 1440.0F;
    }
    while (v >= 1440.0F) {
        v -= 1440.0F;
    }
    return static_cast<int16_t>(lroundf(v));
}

}  // namespace

auto SunPhase::compute(float latitude, float longitude, const tm& localDate, int32_t utcOffsetMin) -> Times {
    const Solar s = solarForDay(localDate.tm_yday);
    const float latRad = latitude * DEG;
    const float haSun = hourAngle(ZENITH_SUNRISE, latRad, s.decl);
    const float haCivil = hourAngle(ZENITH_CIVIL, latRad, s.decl);

    Times t{};
    if (std::isnan(haSun) || std::isnan(haCivil)) {
        return t;
    }
    const float noon = 720.0F - 4.0F * longitude - s.eqTime + static_cast<float>(utcOffsetMin);
    t.dawn = toMinutes(noon - 4.0F * haCivil);
    t.sunrise = toMinutes(noon - 4.0F * haSun);
    t.sunset = toMinutes(noon + 4.0F * haSun);
    t.dusk = toMinutes(noon + 4.0F * haCivil);
    t.valid = true;
    return t;
}

auto SunPhase::phaseAt(const Times& t, int16_t m) -> Phase {
    if (!t.valid) {
        return Phase::Day;
    }
    if (m < t.dawn || m >= t.dusk) {
        return Phase::Night;
    }
    if (m < t.sunrise + MORNING_AFTER_SUNRISE_MIN) {
        return Phase::Morning;
    }
    if (m < t.sunset - EVENING_BEFORE_SUNSET_MIN) {
        return Phase::Day;
    }
    return Phase::Evening;
}

auto SunPhase::name(Phase phase) -> const char* {
    switch (phase) {
        case Phase::Night:
            return "night";
        case Phase::Morning:
            return "morning";
        case Phase::Evening:
            return "evening";
        default:
            return "day";
    }
}
