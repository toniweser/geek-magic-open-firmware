// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

/**
 * @brief The handful of weather situations the aquarium reacts to, derived from WMO codes
 */
namespace Weather {

enum class Kind : uint8_t { Clear, Cloudy, Fog, Rain, Snow, Thunder };

struct Condition {
    Kind kind = Kind::Clear;
    uint8_t level = 1;  // 1 light, 2 moderate, 3 heavy: particle count and flash strength
};

/** Map an Open-Meteo `weather_code` (WMO 4677 subset) to a condition. */
inline Condition fromWmo(uint8_t code) {
    switch (code) {
        case 0:
        case 1:
            return {Kind::Clear, 1};
        case 2:
            return {Kind::Cloudy, 1};
        case 3:
            return {Kind::Cloudy, 2};
        case 45:
        case 48:
            return {Kind::Fog, 2};
        case 51:
        case 56:
        case 61:
        case 66:
        case 80:
            return {Kind::Rain, 1};
        case 53:
        case 63:
        case 81:
            return {Kind::Rain, 2};
        case 55:
        case 57:
        case 65:
        case 67:
        case 82:
            return {Kind::Rain, 3};
        case 71:
        case 77:
            return {Kind::Snow, 1};
        case 73:
        case 85:
            return {Kind::Snow, 2};
        case 75:
        case 86:
            return {Kind::Snow, 3};
        case 95:
            return {Kind::Thunder, 2};
        case 96:
        case 99:
            return {Kind::Thunder, 3};
        default:
            return {Kind::Cloudy, 2};
    }
}

inline const char* name(Kind kind) {
    switch (kind) {
        case Kind::Clear:
            return "clear";
        case Kind::Cloudy:
            return "cloudy";
        case Kind::Fog:
            return "fog";
        case Kind::Rain:
            return "rain";
        case Kind::Snow:
            return "snow";
        default:
            return "thunder";
    }
}

inline bool parse(const char* text, Kind& out) {
    static constexpr Kind ALL[] = {Kind::Clear, Kind::Cloudy, Kind::Fog, Kind::Rain, Kind::Snow, Kind::Thunder};
    if (text == nullptr) {
        return false;
    }
    for (Kind k : ALL) {
        if (strcmp(text, name(k)) == 0) {
            out = k;
            return true;
        }
    }
    return false;
}

}  // namespace Weather
