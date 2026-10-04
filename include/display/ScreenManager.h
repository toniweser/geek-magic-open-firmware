// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <array>
#include "display/screens/Screen.h"

/**
 * @brief Owns the active screen and ticks it from the main loop
 *
 * Stays out of the way while a GIF is playing or while nothing has been
 * started yet, so the Times-Z startup screen and GIF player keep working.
 * Screens register by name so the API can switch between them.
 */
class ScreenManager {
   public:
    static constexpr size_t MAX_SCREENS = 8;

    static void registerScreen(Screen* screen);
    static void begin(Screen* initial);
    static void loop();

    /** Switch to another registered screen (onExit on the old one, onEnter on the new one). */
    static bool show(Screen* screen);
    static Screen* find(const char* name);

    /** Force a full repaint, e.g. after rotation changed or a GIF stopped. */
    static void invalidate();

    static Screen* active();
    static const std::array<Screen*, MAX_SCREENS>& screens();
    static size_t screenCount();

   private:
    static std::array<Screen*, MAX_SCREENS> _screens;
    static size_t _count;
    static Screen* _active;
    static bool _needsEnter;
    static bool _suspendedByGif;
};
