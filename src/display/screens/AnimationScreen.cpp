// SPDX-License-Identifier: GPL-3.0-or-later
#include "display/screens/AnimationScreen.h"

#include <Logger.h>
#include <cmath>
#include <cstring>

#include "config/ConfigManager.h"
#include "display/ColorProfile.h"
#include "display/Palette.h"

namespace {

constexpr const char* TAG = "Animation";
constexpr size_t HEADER_SIZE = 16;
constexpr size_t MAX_FRAME_BYTES = 8192;
constexpr unsigned long MAX_LAG_MS = 500;
constexpr uint8_t FLAG_RLE = 0x01;

auto readU16(const uint8_t* p) -> uint16_t { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

struct Rgb {
    float r;
    float g;
    float b;
};

auto from565(uint16_t c) -> Rgb {
    return {static_cast<float>((c >> 11) << 3), static_cast<float>(((c >> 5) & 0x3F) << 2),
            static_cast<float>((c & 0x1F) << 3)};
}

auto to565(const Rgb& c) -> uint16_t {
    const auto r = static_cast<uint16_t>(fminf(255.0F, fmaxf(0.0F, c.r)));
    const auto g = static_cast<uint16_t>(fminf(255.0F, fmaxf(0.0F, c.g)));
    const auto b = static_cast<uint16_t>(fminf(255.0F, fmaxf(0.0F, c.b)));
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Integer blend of two RGB565 colors, alpha 0..255 weighs `top`
auto blend565(uint16_t base, uint16_t top, uint8_t alpha) -> uint16_t {
    const uint32_t a = alpha;
    const uint32_t ia = 255 - a;
    const uint32_t r = (((base >> 11) & 0x1F) * ia + ((top >> 11) & 0x1F) * a) / 255;
    const uint32_t g = (((base >> 5) & 0x3F) * ia + ((top >> 5) & 0x3F) * a) / 255;
    const uint32_t b = ((base & 0x1F) * ia + (top & 0x1F) * a) / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}  // namespace

auto AnimationScreen::setFile(const String& path) -> void { _path = path; }

auto AnimationScreen::open() -> bool {
    close();
    _error = "";

    if (_path.length() == 0) {
        _error = "no file selected";
        return false;
    }

    _file = LittleFS.open(_path, "r");
    if (!_file) {
        _error = "cannot open " + _path;
        return false;
    }

    std::array<uint8_t, HEADER_SIZE> hdr{};
    if (_file.read(hdr.data(), hdr.size()) != hdr.size()) {
        _error = "truncated header";
        return false;
    }
    const bool v1 = memcmp(hdr.data(), "PXA1", 4) == 0;
    const bool v2 = memcmp(hdr.data(), "PXA2", 4) == 0;
    if (!v1 && !v2) {
        _error = "not a PXA file";
        return false;
    }

    _width = readU16(&hdr[4]);
    _height = readU16(&hdr[6]);
    _scale = hdr[8];
    _fps = hdr[9];
    _frameCount = readU16(&hdr[10]);
    _bpp = hdr[12];
    _colorCount = hdr[13];
    _rle = v2 && (hdr[14] & FLAG_RLE) != 0;

    if (_width == 0 || _height == 0 || _scale == 0 || _fps == 0 || _frameCount == 0 || (_bpp != 4 && _bpp != 8) ||
        (_rle && _bpp != 8) || _colorCount == 0 || _width * _scale > LCD_W || _height * _scale > LCD_H) {
        _error = "bad header";
        return false;
    }

    _frameBytes = (static_cast<size_t>(_width) * _height * _bpp + 7) / 8;
    if (_frameBytes > MAX_FRAME_BYTES) {
        _error = "frame too large";
        return false;
    }

    _paletteSrc.reset(new (std::nothrow) uint16_t[_colorCount]);
    _palette.reset(new (std::nothrow) uint16_t[_colorCount]);
    _frame.reset(new (std::nothrow) uint8_t[_frameBytes]);
    _line.reset(new (std::nothrow) uint16_t[static_cast<size_t>(_width) * _scale]);
    if (!_paletteSrc || !_palette || !_frame || !_line || !_overlay.begin(_width, _height)) {
        _error = "out of memory";
        return false;
    }

    std::array<uint8_t, 512> palBuf{};
    const size_t palBytes = static_cast<size_t>(_colorCount) * 2;
    if (_file.read(palBuf.data(), palBytes) != palBytes) {
        _error = "truncated palette";
        return false;
    }
    for (size_t i = 0; i < _colorCount; i++) {
        _paletteSrc[i] = readU16(&palBuf[i * 2]);
    }
    applyMood(WeatherLayer::moodFor(WeatherLayer::current().kind));

    _dataOffset = HEADER_SIZE + palBytes;
    _frameIntervalMs = 1000UL / _fps;
    _frameIndex = 0;
    _rdPos = _rdLen = 0;
    _x0 = static_cast<int16_t>((LCD_W - _width * _scale) / 2);
    _y0 = static_cast<int16_t>((LCD_H - _height * _scale) / 2);

    std::array<char, 112> msg{};
    snprintf(msg.data(), msg.size(), "%s: %ux%u x%u, %u fps, %u frames, %u colors, %s", _path.c_str(), _width, _height,
             _scale, _fps, _frameCount, _colorCount, _rle ? "rle" : "raw");
    Logger::info(msg.data(), TAG);

    return true;
}

auto AnimationScreen::close() -> void {
    if (_file) {
        _file.close();
    }
    _frame.reset();
    _line.reset();
    _palette.reset();
    _paletteSrc.reset();
    _overlay.release();
    _ok = false;
}

auto AnimationScreen::applyMood(const Mood& m) -> void {
    for (size_t i = 0; i < _colorCount; i++) {
        Rgb c = from565(_paletteSrc[i]);
        const float gray = c.r * 0.3F + c.g * 0.59F + c.b * 0.11F;
        c.r = (c.r * (1 - m.desat) + gray * m.desat) * m.dim;
        c.g = (c.g * (1 - m.desat) + gray * m.desat) * m.dim;
        c.b = (c.b * (1 - m.desat) + gray * m.desat) * m.dim;
        if (m.tint > 0) {
            c.r = c.r * (1 - m.tint) + m.tintR * m.tint;
            c.g = c.g * (1 - m.tint) + m.tintG * m.tint;
            c.b = c.b * (1 - m.tint) + m.tintB * m.tint;
        }
        if (m.flash > 0) {
            c.r += (255 - c.r) * m.flash;
            c.g += (255 - c.g) * m.flash;
            c.b += (255 - c.b) * m.flash;
        }
        ColorProfile::apply(c.r, c.g, c.b);
        _palette[i] = to565(c);
    }
}

auto AnimationScreen::readByte() -> int {
    if (_rdPos >= _rdLen) {
        _rdLen = _file.read(_rd.data(), _rd.size());
        _rdPos = 0;
        if (_rdLen == 0) {
            return -1;
        }
    }
    return _rd[_rdPos++];
}

auto AnimationScreen::readRleFrame() -> bool {
    const size_t pixels = static_cast<size_t>(_width) * _height;
    size_t filled = 0;
    while (filled < pixels) {
        const int len = readByte();
        const int idx = readByte();
        if (len < 0 || idx < 0) {
            return false;
        }
        const size_t run = static_cast<size_t>(len) + 1;
        if (filled + run > pixels) {
            return false;
        }
        memset(_frame.get() + filled, idx, run);
        filled += run;
    }
    return true;
}

auto AnimationScreen::readFrame() -> bool {
    if (_frameIndex == 0) {
        _file.seek(_dataOffset, SeekSet);
        _rdPos = _rdLen = 0;
    }
    const bool ok = _rle ? readRleFrame() : (_file.read(_frame.get(), _frameBytes) == _frameBytes);
    if (!ok) {
        _error = "read error";
        return false;
    }
    _frameIndex = static_cast<uint16_t>((_frameIndex + 1) % _frameCount);
    return true;
}

auto AnimationScreen::drawFrame(Arduino_GFX* gfx) -> void {
    const auto outW = static_cast<int16_t>(_width * _scale);
    uint16_t* line = _line.get();

    for (uint16_t y = 0; y < _height; y++) {
        uint16_t* dst = line;
        for (uint16_t x = 0; x < _width; x++) {
            const size_t px = static_cast<size_t>(y) * _width + x;
            uint8_t idx = 0;
            if (_bpp == 4) {
                const uint8_t b = _frame[px >> 1];
                idx = (px & 1) ? (b & 0x0F) : (b >> 4);
            } else {
                idx = _frame[px];
            }
            uint16_t color = (idx < _colorCount) ? _palette[idx] : 0;
            const uint8_t ov = _overlay.at(px);
            if (ov != 0) {
                const OverlayBuffer::Entry& e = _overlay.entry(ov);
                color = (e.alpha == 255) ? e.color : blend565(color, e.color, e.alpha);
            }
            for (uint8_t s = 0; s < _scale; s++) {
                *dst++ = color;
            }
        }
        const auto rowY = static_cast<int16_t>(_y0 + y * _scale);
        for (uint8_t s = 0; s < _scale; s++) {
            gfx->draw16bitRGBBitmap(_x0, static_cast<int16_t>(rowY + s), line, outW, 1);
        }
    }
}

auto AnimationScreen::drawError(Arduino_GFX* gfx) -> void {
    gfx->fillScreen(Palette::BG);
    gfx->setTextSize(1);
    gfx->setFont(static_cast<const GFXfont*>(nullptr));
    gfx->setTextColor(Palette::TEXT);
    gfx->setCursor(8, 100);
    gfx->print("Animation:");
    gfx->setCursor(8, 112);
    gfx->print(_error);
    Logger::warn(_error.c_str(), TAG);
}

auto AnimationScreen::onEnter(Arduino_GFX* gfx) -> void {
    gfx->fillScreen(Palette::BG);
    _ok = open();
    if (!_ok) {
        close();
        drawError(gfx);
        return;
    }
    _nextFrameMs = millis();
}

auto AnimationScreen::update(Arduino_GFX* gfx, unsigned long nowMs) -> void {
    if (!_ok || static_cast<long>(nowMs - _nextFrameMs) < 0) {
        return;
    }

    if (!readFrame()) {
        _ok = false;
        close();
        drawError(gfx);
        return;
    }

    WeatherLayer::frame(_overlay, _night);
    Mood mood;
    if (WeatherLayer::takeMoodChange(mood)) {
        applyMood(mood);
    }
    drawFrame(gfx);

    _nextFrameMs += _frameIntervalMs;
    if (static_cast<long>(nowMs - _nextFrameMs) > static_cast<long>(MAX_LAG_MS)) {
        _nextFrameMs = nowMs;  // fell behind (OTA, HTTP); skip ahead instead of racing
    }
}

auto AnimationScreen::onExit(Arduino_GFX* /*gfx*/) -> void { close(); }
