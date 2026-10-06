// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <LittleFS.h>
#include <array>
#include <memory>
#include "display/Overlay.h"
#include "display/screens/Screen.h"
#include "weather/WeatherLayer.h"

/**
 * @brief Full-screen player for PXA pixel animations from LittleFS
 *
 * PXA is our own format (see tools/pxa.py): a palette plus 4- or 8-bit
 * frames on a small grid that is scaled up while drawing, optionally
 * run-length encoded. One frame, one output line and a small overlay live
 * in RAM, a few KB in total. That is the whole point: a GIF decoder needs
 * 24 KB of tables, which this device does not have to spare.
 *
 * The weather layer modulates the palette (mood) and draws its props into
 * the overlay, which is merged while each line is expanded.
 */
class AnimationScreen : public Screen {
   public:
    const char* name() const override { return "animation"; }
    void onEnter(Arduino_GFX* gfx) override;
    void update(Arduino_GFX* gfx, unsigned long nowMs) override;
    void onExit(Arduino_GFX* gfx) override;

    /** Path inside LittleFS, e.g. "/gif/day.pxa". Takes effect on the next onEnter(). */
    void setFile(const String& path);
    const String& file() const { return _path; }
    const String& lastError() const { return _error; }

    /** Whether the overlay should draw its night props (stars). Set by the scheduler. */
    void setNight(bool night) { _night = night; }

   private:
    String _path;
    String _error;
    File _file;
    bool _ok = false;
    bool _night = false;

    uint16_t _width = 0;
    uint16_t _height = 0;
    uint8_t _scale = 1;
    uint8_t _fps = 10;
    uint16_t _frameCount = 0;
    uint8_t _bpp = 4;
    uint8_t _colorCount = 0;
    bool _rle = false;
    size_t _frameBytes = 0;
    size_t _dataOffset = 0;
    int16_t _x0 = 0;
    int16_t _y0 = 0;

    uint16_t _frameIndex = 0;
    unsigned long _nextFrameMs = 0;
    unsigned long _frameIntervalMs = 100;

    std::unique_ptr<uint8_t[]> _frame;        // one unpacked (8 bpp) or packed (4 bpp) source frame
    std::unique_ptr<uint16_t[]> _line;        // one output line, RGB565
    std::unique_ptr<uint16_t[]> _paletteSrc;  // palette as stored in the file
    std::unique_ptr<uint16_t[]> _palette;     // palette with the current mood applied
    OverlayBuffer _overlay;

    // small read-ahead buffer for run-length frames, avoids a file call per run
    std::array<uint8_t, 256> _rd{};
    size_t _rdPos = 0;
    size_t _rdLen = 0;

    bool open();
    void close();
    bool readFrame();
    bool readRleFrame();
    int readByte();
    void applyMood(const Mood& mood);
    void drawFrame(Arduino_GFX* gfx);
    void drawError(Arduino_GFX* gfx);
};
