// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <Arduino.h>

/**
 * @brief Saturation and contrast boost for the panel
 *
 * The IPS panel and RGB565 wash colors out compared to a monitor. The
 * profile is applied wherever a color enters the display path: the
 * animation palette (once per mood change) and the overlay palette (a few
 * entries per frame), so it costs nothing per pixel.
 */
namespace ColorProfile {

/** 1.0 leaves colors alone; the SmallTV-Ultra defaults to 1.35 / 1.15. */
void set(float saturation, float contrast);
float saturation();
float contrast();

/** Apply to 8-bit components in place. */
void apply(float& r, float& g, float& b);

}  // namespace ColorProfile
