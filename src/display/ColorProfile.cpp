// SPDX-License-Identifier: GPL-3.0-or-later
#include "display/ColorProfile.h"

#include <cmath>

namespace {
float g_saturation = 1.0F;
float g_contrast = 1.0F;

auto clamp8(float v) -> float { return fminf(255.0F, fmaxf(0.0F, v)); }
}  // namespace

auto ColorProfile::set(float saturation, float contrast) -> void {
    g_saturation = fminf(3.0F, fmaxf(0.0F, saturation));
    g_contrast = fminf(3.0F, fmaxf(0.1F, contrast));
}

auto ColorProfile::saturation() -> float { return g_saturation; }

auto ColorProfile::contrast() -> float { return g_contrast; }

auto ColorProfile::apply(float& r, float& g, float& b) -> void {
    if (g_saturation == 1.0F && g_contrast == 1.0F) {
        return;
    }
    // saturation: push each channel away from the luminance
    const float gray = r * 0.3F + g * 0.59F + b * 0.11F;
    r = gray + (r - gray) * g_saturation;
    g = gray + (g - gray) * g_saturation;
    b = gray + (b - gray) * g_saturation;
    // contrast: stretch around mid grey
    r = clamp8((r - 128.0F) * g_contrast + 128.0F);
    g = clamp8((g - 128.0F) * g_contrast + 128.0F);
    b = clamp8((b - 128.0F) * g_contrast + 128.0F);
}
