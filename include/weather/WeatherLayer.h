// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <array>
#include "display/Overlay.h"
#include "weather/WeatherKind.h"

/**
 * @brief Palette mood plus overlay pixels per animation frame
 *
 * The mood (dim, desaturate, tint, flash) is applied by the player to its
 * palette once per change. The overlay holds the pixel props drawn over the
 * scene: a cloud bank, rain, snow with a snow cover and a snowman, a fog
 * band with a lighthouse, stars at night. Per-frame speeds assume 24 fps.
 */
struct Mood {
    float dim = 1.0F;
    float desat = 0.0F;
    uint8_t tintR = 0;
    uint8_t tintG = 0;
    uint8_t tintB = 0;
    float tint = 0.0F;
    float flash = 0.0F;
};

class WeatherLayer {
   public:
    static void begin();

    /** Condition in effect: the override when set, else the live forecast, else clear. */
    static Weather::Condition current();
    static void setOverride(bool on, Weather::Condition condition = {});
    static bool overrideActive();

    /** Fill the overlay for the next frame and advance particles. `night` draws stars when clear. */
    static void frame(OverlayBuffer& overlay, bool night);

    /** True once after the mood changed; the caller re-applies it to the palette. */
    static bool takeMoodChange(Mood& out);
    static Mood moodFor(Weather::Kind kind);

   private:
    struct Cloud {
        float x;
        float v;
        uint8_t w;
        uint8_t y;
    };
    struct Drop {
        float x;
        float y;
        float v;
        uint8_t len;
    };
    struct Flake {
        float x;
        float y;
        float v;
        float ph;
        bool big;
    };
    struct Star {
        uint8_t x;
        uint8_t y;
        float ph;
    };

    static constexpr uint8_t MAX_CLOUDS = 3;
    static constexpr uint8_t MAX_DROPS = 40;
    static constexpr uint8_t MAX_FLAKES = 28;
    static constexpr uint8_t STARS = 9;

    static bool _override;
    static Weather::Condition _overrideCond;
    static Weather::Kind _lastKind;
    static bool _moodDirty;
    static float _flash;
    static int _nextFlash;
    static uint32_t _t;
    static float _beam;
    static std::array<Cloud, MAX_CLOUDS> _clouds;
    static uint8_t _cloudCount;
    static std::array<Drop, MAX_DROPS> _drops;
    static uint8_t _dropCount;
    static std::array<Flake, MAX_FLAKES> _flakes;
    static uint8_t _flakeCount;
    static std::array<Star, STARS> _stars;
    static int _shootLife;
    static float _shootX;
    static float _shootY;

    static void clouds(OverlayBuffer& ov, Weather::Kind kind, int n);
    static void rain(OverlayBuffer& ov, uint8_t level, int n, int cloudBottom);
    static void snow(OverlayBuffer& ov, uint8_t level, int n, int cloudBottom);
    static void snowGround(OverlayBuffer& ov, int n);
    static void snowman(OverlayBuffer& ov, int x, int base);
    static void fogBand(OverlayBuffer& ov, int n);
    static void lighthouse(OverlayBuffer& ov, int n);
    static void stars(OverlayBuffer& ov, int n);
    static void thunder(uint8_t level);
};
