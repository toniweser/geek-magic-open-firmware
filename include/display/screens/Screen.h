// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

/**
 * @brief Base class for everything that owns the display for a while
 *
 * The ESP8266 has no room for a 240x240 framebuffer, so a screen must only
 * repaint the regions that actually changed. `onEnter()` paints everything
 * once; `update()` is called every main-loop iteration and must return fast.
 */
class Screen {
   public:
    virtual ~Screen() = default;

    virtual const char* name() const = 0;

    /** Full repaint. Called when the screen becomes active or after invalidate(). */
    virtual void onEnter(Arduino_GFX* gfx) = 0;

    /** Incremental repaint. Must only touch regions whose content changed. */
    virtual void update(Arduino_GFX* gfx, unsigned long nowMs) = 0;

    virtual void onExit(Arduino_GFX* /*gfx*/) {}
};
