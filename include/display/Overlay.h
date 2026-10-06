// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>
#include <array>
#include <cstring>
#include <memory>

/**
 * @brief Sparse pixel layer drawn over an animation frame while it is expanded
 *
 * Four bits per grid pixel index a tiny palette of up to 15 entries, each an
 * RGB565 color with an alpha. The animation player consults the buffer for
 * every pixel it expands, so overlay pixels never flicker and translucent
 * entries blend with whatever the frame shows underneath. A 60x60 grid costs
 * 1.8 KB.
 */
class OverlayBuffer {
   public:
    struct Entry {
        uint16_t color = 0;
        uint8_t alpha = 255;  // 255 = opaque
    };

    static constexpr uint8_t MAX_ENTRIES = 15;

    bool begin(uint16_t width, uint16_t height) {
        _w = width;
        _h = height;
        _nibbles.reset(new (std::nothrow) uint8_t[(static_cast<size_t>(width) * height + 1) / 2]);
        clear();
        return static_cast<bool>(_nibbles);
    }

    void release() {
        _nibbles.reset();
        _w = _h = 0;
    }

    bool ready() const { return static_cast<bool>(_nibbles); }
    uint16_t width() const { return _w; }
    uint16_t height() const { return _h; }

    /** Empty the layer and its palette; call once per frame before drawing into it. */
    void clear() {
        if (_nibbles) {
            memset(_nibbles.get(), 0, (static_cast<size_t>(_w) * _h + 1) / 2);
        }
        _count = 0;
    }

    /** Register a color, returns its index (1..15) or 0 when the palette is full. Same color+alpha is reused. */
    uint8_t color(uint16_t rgb565, uint8_t alpha = 255) {
        for (uint8_t i = 0; i < _count; i++) {
            if (_pal.at(i).color == rgb565 && _pal.at(i).alpha == alpha) {
                return static_cast<uint8_t>(i + 1);
            }
        }
        if (_count >= MAX_ENTRIES) {
            return 0;
        }
        _pal.at(_count) = {rgb565, alpha};
        _count++;
        return _count;
    }

    void set(int x, int y, uint8_t idx) {
        if (!_nibbles || idx == 0 || x < 0 || y < 0 || x >= _w || y >= _h) {
            return;
        }
        const size_t px = static_cast<size_t>(y) * _w + static_cast<size_t>(x);
        uint8_t& b = _nibbles[px >> 1];
        b = (px & 1) ? static_cast<uint8_t>((b & 0xF0) | idx) : static_cast<uint8_t>((b & 0x0F) | (idx << 4));
    }

    void rect(int x, int y, int w, int h, uint8_t idx) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                set(x + i, y + j, idx);
            }
        }
    }

    /** Index at a linear pixel position, 0 when the frame shows through. */
    uint8_t at(size_t px) const {
        const uint8_t b = _nibbles[px >> 1];
        return (px & 1) ? (b & 0x0F) : (b >> 4);
    }

    const Entry& entry(uint8_t idx) const { return _pal.at(idx - 1); }

   private:
    std::unique_ptr<uint8_t[]> _nibbles;
    std::array<Entry, MAX_ENTRIES> _pal{};
    uint8_t _count = 0;
    uint16_t _w = 0;
    uint16_t _h = 0;
};
