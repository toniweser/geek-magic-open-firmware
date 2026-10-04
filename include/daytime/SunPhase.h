// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <ctime>

/**
 * @brief Sunrise, sunset and civil twilight for a location, and the part of
 *        the day they define
 *
 * NOAA's solar position approximation, good to a minute or two, which is
 * plenty for switching scenes. All times are minutes after local midnight.
 */
namespace SunPhase {

enum class Phase : uint8_t { Night, Morning, Day, Evening };

struct Times {
    int16_t dawn = 0;     // civil twilight begins (sun 6 degrees below the horizon)
    int16_t sunrise = 0;
    int16_t sunset = 0;
    int16_t dusk = 0;     // civil twilight ends
    bool valid = false;   // false near the poles when the sun never rises or sets
};

/** @param utcOffsetMin local time minus UTC in minutes (handles DST when taken from localtime) */
Times compute(float latitude, float longitude, const tm& localDate, int32_t utcOffsetMin);

/** Morning runs from dawn to two hours after sunrise, evening from two hours before sunset to dusk. */
Phase phaseAt(const Times& t, int16_t minutesOfDay);

const char* name(Phase phase);

}  // namespace SunPhase
