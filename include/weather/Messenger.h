// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <array>
#include "display/Overlay.h"

/**
 * @brief Figures that carry the outdoor temperature through the aquarium
 *
 * Nine messengers take turns: a crab with a sign, a submarine, a puffer
 * fish, a treasure chest, a message in a bottle, an octopus writing with
 * ink, a fishing hook from a boat, a sunken neon sign and a crab parade.
 * Each one is an overlay prop drawn after the weather props. One pass takes
 * 6 to 35 s, then the next messenger follows after at most four seconds.
 * The temperature is the Open-Meteo value; nothing runs until the first
 * fetch succeeded. Text uses a 3x5 pixel font.
 *
 * In the normal mode only three of the nine run on any given day. The trio
 * is drawn from the date (a day runs from 4:00 to 4:00 local time) and the
 * chip id, so a reboot keeps the same three; without a valid clock all nine
 * take turns.
 */
class Messenger {
   public:
    enum class Kind : uint8_t { Crab, Sub, Puffer, Chest, Bottle, Octo, Hook, Neon, Parade };
    static constexpr uint8_t KIND_COUNT = 9;
    enum class Mode : uint8_t { Random, Demo, Fixed };

    static void begin();

    /** Advance the state by one frame and register this frame's colors; runs before the weather props so the figure never runs out of palette entries. */
    static void advance(OverlayBuffer& overlay, bool night, bool snow);

    /** Draw the current messenger over the weather props; with `snow` ground props stand on the snow cover. */
    static void draw(OverlayBuffer& overlay, bool night, bool snow);

    /** On unless switched off for this session or in the config. */
    static bool enabled();
    /** Switch for this session; with `persist` the config key follows and is saved by the caller. */
    static void setEnabled(bool on, bool persist);
    static bool visible() { return _active; }

    /** Random picks from today's three per pass, Demo cycles through all nine in order, Fixed repeats one. */
    static void setMode(Mode mode, Kind fixed = Kind::Crab);
    static constexpr uint8_t DAILY_COUNT = 3;
    /** Today's trio, false while the clock is not synced yet. */
    static bool today(std::array<Kind, DAILY_COUNT>& out);
    /** Draw a new trio right away (for testing); the next 4:00 change draws from the date again. */
    static void reroll();
    static const char* modeName();
    static const char* currentName();
    static const char* kindName(Kind kind);
    static bool parseKind(const char* text, Kind& out);

    /** Cut the running pass short and start the next one right away (for testing). */
    static void triggerNow();

    /** Force a temperature for testing; NAN returns to the live value. */
    static void setOverride(float celsius);
    static bool overrideActive();

    /** The value on display, NAN when nothing is shown. */
    static float shownTemperature();

   private:
    static constexpr uint8_t TEXT_MAX = 5;  // "-12°" plus terminator

    struct Bubble {
        float x;
        float y;
        uint8_t life;
    };

    static int8_t _forced;
    static Mode _mode;
    static Kind _fixed;
    static Kind _kind;
    static uint8_t _demoIndex;
    static int32_t _dayKey;        // the day the trio below was drawn for, -1 = none
    static uint8_t _rerollSalt;    // bumps the draw within one day
    static std::array<Kind, DAILY_COUNT> _trio;
    static int8_t _lastPick;       // index into the trio, avoids an immediate repeat
    static bool _active;
    static uint8_t _phase;
    static int16_t _timer;
    static uint32_t _t;
    static int _wait;
    static float _x;
    static float _y;
    static float _a;
    static float _b;
    static float _c;
    static int8_t _look;
    static int16_t _tx;
    static int8_t _dir;
    static std::array<Bubble, 7> _bub;  // one spawns every 8 frames and lives 56
    static uint8_t _bubN;
    static float _override;
    static float _shown;
    static char _text[TEXT_MAX];
    static uint8_t _textWidth;

    static bool prepareText();
    static int32_t dayKey();       // -1 without a valid clock
    static void drawTrio(int32_t key);
    static Kind pickDaily();
    static void start(int n, int ground);
    static void render(int ground);
    static bool step(int n, int ground);  // false when the pass is over
    static void gaitStep();               // crab walk shared by the crab and the parade
};
