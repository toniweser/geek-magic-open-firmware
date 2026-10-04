// SPDX-License-Identifier: GPL-3.0-or-later
#include "display/screens/AnimationScreen.h"

#include <Logger.h>
#include <cstring>

#include "config/ConfigManager.h"
#include "display/Palette.h"

namespace {

constexpr const char* TAG = "Animation";
constexpr size_t HEADER_SIZE = 16;
constexpr size_t MAX_FRAME_BYTES = 8192;
constexpr unsigned long MAX_LAG_MS = 500;
constexpr uint8_t FLAG_RLE = 0x01;

auto readU16(const uint8_t* p) -> uint16_t { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

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

    _palette.reset(new (std::nothrow) uint16_t[_colorCount]);
    _frame.reset(new (std::nothrow) uint8_t[_frameBytes]);
    _block.reset(new (std::nothrow) uint16_t[static_cast<size_t>(_width) * _scale * _scale]);
    if (!_palette || !_frame || !_block) {
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
        _palette[i] = readU16(&palBuf[i * 2]);
    }

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
    _block.reset();
    _palette.reset();
    _ok = false;
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
    const size_t outW = static_cast<size_t>(_width) * _scale;
    uint16_t* line0 = _block.get();

    for (uint16_t y = 0; y < _height; y++) {
        // expand one source row into the first output line
        uint16_t* dst = line0;
        for (uint16_t x = 0; x < _width; x++) {
            const size_t px = static_cast<size_t>(y) * _width + x;
            uint8_t idx = 0;
            if (_bpp == 4) {
                const uint8_t b = _frame[px >> 1];
                idx = (px & 1) ? (b & 0x0F) : (b >> 4);
            } else {
                idx = _frame[px];
            }
            const uint16_t color = (idx < _colorCount) ? _palette[idx] : 0;
            for (uint8_t s = 0; s < _scale; s++) {
                *dst++ = color;
            }
        }
        // replicate it `scale` times, then push the block in one go
        for (uint8_t s = 1; s < _scale; s++) {
            memcpy(line0 + s * outW, line0, outW * sizeof(uint16_t));
        }
        gfx->draw16bitRGBBitmap(_x0, static_cast<int16_t>(_y0 + y * _scale), line0, static_cast<int16_t>(outW),
                                _scale);
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
    drawFrame(gfx);

    _nextFrameMs += _frameIntervalMs;
    if (static_cast<long>(nowMs - _nextFrameMs) > static_cast<long>(MAX_LAG_MS)) {
        _nextFrameMs = nowMs;  // fell behind (OTA, HTTP); skip ahead instead of racing
    }
}

auto AnimationScreen::onExit(Arduino_GFX* /*gfx*/) -> void { close(); }
