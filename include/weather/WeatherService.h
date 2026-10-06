// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

struct WeatherData {
    bool valid = false;
    float tempCurrent = 0.0F;
    uint8_t wmoCode = 0;
    bool isDay = true;
    uint32_t revision = 0;  // bumped on every successful fetch
};

/**
 * @brief Pulls current weather and today's min/max from Open-Meteo over plain HTTP
 *
 * HTTPS is out of reach on the ESP8266 heap budget, Open-Meteo works without
 * TLS and without an API key. Fetches run from loop() and block for the
 * duration of one request (a second or two), which the main loop tolerates.
 */
class WeatherService {
   public:
    static void begin();
    static void loop();
    static bool fetchNow();
    static const WeatherData& data();
    static unsigned long lastAttemptMs();
    static const char* lastStatus();

   private:
    static WeatherData _data;
    static unsigned long _nextAttemptMs;
    static unsigned long _lastAttemptMs;
    static String _lastStatus;
};
