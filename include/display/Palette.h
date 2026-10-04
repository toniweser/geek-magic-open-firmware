// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// RGB565 colors shared by the screens
namespace Palette {
constexpr uint16_t BG = 0x0000;    // #000000
constexpr uint16_t TEXT = 0x9D15;  // #9aa3ad, muted grey for status messages
}  // namespace Palette
