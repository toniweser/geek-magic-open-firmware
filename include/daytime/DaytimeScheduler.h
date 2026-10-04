// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include "daytime/SunPhase.h"
#include "display/screens/AnimationScreen.h"

/**
 * @brief Picks the animation for the current part of the day
 *
 * Checks the sun phase every few seconds and swaps the animation file when
 * it changes (a hard cut). Until NTP has delivered the time, the day scene
 * plays. A manual file switch through the API pauses the scheduler until
 * it is enabled again or the device reboots.
 */
class DaytimeScheduler {
   public:
    static void begin(AnimationScreen* screen);
    static void loop();

    static void setEnabled(bool enabled);
    static bool enabled();

    /** Test aid: cycle through the four scenes every `ms` milliseconds, 0 returns to the sun. */
    static void setDemo(unsigned long ms);
    static unsigned long demoIntervalMs();

    static bool timeSynced();
    static SunPhase::Phase phase();
    static const SunPhase::Times& times();

    /** Resolve the configured file name for a phase to a LittleFS path. */
    static String fileFor(SunPhase::Phase phase);

   private:
    static AnimationScreen* _screen;
    static bool _enabled;
    static bool _applied;
    static SunPhase::Phase _phase;
    static SunPhase::Times _times;
    static int _timesForDay;
    static unsigned long _nextCheckMs;
    static unsigned long _demoMs;

    static void apply(SunPhase::Phase phase);
};
