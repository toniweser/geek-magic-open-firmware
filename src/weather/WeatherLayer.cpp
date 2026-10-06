// SPDX-License-Identifier: GPL-3.0-or-later
#include "weather/WeatherLayer.h"

#include <cmath>

#include "config/ConfigManager.h"
#include "display/ColorProfile.h"
#include "weather/WeatherService.h"

extern ConfigManager configManager;

namespace {

constexpr float TAU = 6.2831853F;

auto rgb(uint8_t r, uint8_t g, uint8_t b) -> uint16_t {
    float fr = r, fg = g, fb = b;
    ColorProfile::apply(fr, fg, fb);
    return static_cast<uint16_t>(((static_cast<uint16_t>(fr) & 0xF8) << 8) | ((static_cast<uint16_t>(fg) & 0xFC) << 3) |
                                 (static_cast<uint16_t>(fb) >> 3));
}

auto frand(float lo, float hi) -> float { return lo + (hi - lo) * static_cast<float>(random(0, 10000)) / 10000.0F; }
auto irand(int lo, int hi) -> int { return static_cast<int>(random(lo, hi + 1)); }

struct CloudStyle {
    uint8_t count;
    uint16_t color;
    uint16_t shade;
};

auto cloudStyle(Weather::Kind kind) -> CloudStyle {
    switch (kind) {
        case Weather::Kind::Cloudy:
            return {3, rgb(168, 174, 186), rgb(128, 134, 148)};
        case Weather::Kind::Rain:
            return {3, rgb(122, 130, 144), rgb(88, 95, 110)};
        case Weather::Kind::Snow:
            return {2, rgb(214, 220, 232), rgb(176, 184, 200)};
        case Weather::Kind::Thunder:
            return {3, rgb(74, 78, 94), rgb(48, 52, 66)};
        default:
            return {0, 0, 0};
    }
}

}  // namespace

bool WeatherLayer::_override = false;
Weather::Condition WeatherLayer::_overrideCond{};
Weather::Kind WeatherLayer::_lastKind = Weather::Kind::Clear;
bool WeatherLayer::_moodDirty = true;
float WeatherLayer::_flash = 0.0F;
int WeatherLayer::_nextFlash = 80;
uint32_t WeatherLayer::_t = 0;
float WeatherLayer::_beam = 0.0F;
std::array<WeatherLayer::Cloud, WeatherLayer::MAX_CLOUDS> WeatherLayer::_clouds{};
uint8_t WeatherLayer::_cloudCount = 0;
std::array<WeatherLayer::Drop, WeatherLayer::MAX_DROPS> WeatherLayer::_drops{};
uint8_t WeatherLayer::_dropCount = 0;
std::array<WeatherLayer::Flake, WeatherLayer::MAX_FLAKES> WeatherLayer::_flakes{};
uint8_t WeatherLayer::_flakeCount = 0;
std::array<WeatherLayer::Star, WeatherLayer::STARS> WeatherLayer::_stars{};
int WeatherLayer::_shootLife = 0;
float WeatherLayer::_shootX = 0.0F;
float WeatherLayer::_shootY = 0.0F;

auto WeatherLayer::begin() -> void {
    for (uint8_t i = 0; i < STARS; i++) {
        _stars.at(i) = {static_cast<uint8_t>(3 + i * 6 + irand(0, 3)), static_cast<uint8_t>(irand(0, 4)), frand(0, 6)};
    }
    _moodDirty = true;
}

auto WeatherLayer::current() -> Weather::Condition {
    if (_override) {
        return _overrideCond;
    }
    if (!configManager.getWeatherMode()) {
        return {Weather::Kind::Clear, 1};
    }
    const WeatherData& w = WeatherService::data();
    return w.valid ? Weather::fromWmo(w.wmoCode) : Weather::Condition{Weather::Kind::Clear, 1};
}

auto WeatherLayer::setOverride(bool on, Weather::Condition condition) -> void {
    _override = on;
    _overrideCond = condition;
}

auto WeatherLayer::overrideActive() -> bool { return _override; }

auto WeatherLayer::moodFor(Weather::Kind kind) -> Mood {
    Mood m;
    switch (kind) {
        case Weather::Kind::Cloudy:
            m.dim = 0.78F;
            m.desat = 0.35F;
            break;
        case Weather::Kind::Fog:
            m.dim = 0.95F;
            m.desat = 0.55F;
            m.tintR = 200, m.tintG = 205, m.tintB = 210, m.tint = 0.22F;
            break;
        case Weather::Kind::Rain:
            m.dim = 0.82F;
            m.desat = 0.25F;
            m.tintR = 120, m.tintG = 140, m.tintB = 170, m.tint = 0.08F;
            break;
        case Weather::Kind::Snow:
            m.dim = 0.90F;
            m.desat = 0.30F;
            m.tintR = 200, m.tintG = 215, m.tintB = 240, m.tint = 0.12F;
            break;
        case Weather::Kind::Thunder:
            m.dim = 0.62F;
            m.desat = 0.35F;
            m.tintR = 90, m.tintG = 100, m.tintB = 140, m.tint = 0.10F;
            break;
        default:
            break;
    }
    return m;
}

auto WeatherLayer::takeMoodChange(Mood& out) -> bool {
    if (!_moodDirty) {
        return false;
    }
    _moodDirty = false;
    out = moodFor(current().kind);
    out.flash = _flash;
    return true;
}

// ---- props --------------------------------------------------------------

auto WeatherLayer::clouds(OverlayBuffer& ov, Weather::Kind kind, int n) -> void {
    const CloudStyle st = cloudStyle(kind);
    while (_cloudCount < st.count) {
        _clouds.at(_cloudCount++) = {frand(-10, static_cast<float>(n)), frand(0.04F, 0.08F),
                                     static_cast<uint8_t>(irand(12, 18)), static_cast<uint8_t>(irand(0, 2))};
    }
    if (_cloudCount > st.count) {
        _cloudCount = st.count;
    }
    const bool lit = _flash > 0.2F;
    const uint8_t col = ov.color(lit ? rgb(230, 232, 240) : st.color);
    const uint8_t shade = ov.color(lit ? rgb(180, 185, 200) : st.shade);
    for (uint8_t i = 0; i < _cloudCount; i++) {
        Cloud& c = _clouds.at(i);
        c.x -= c.v;
        if (c.x + c.w < -2) {
            c.x = static_cast<float>(n) + frand(2, 12);
            c.w = static_cast<uint8_t>(irand(12, 18));
            c.y = static_cast<uint8_t>(irand(0, 2));
        }
        const int x = static_cast<int>(lroundf(c.x));
        const int y = c.y;
        const int w = c.w;
        const int bump = static_cast<int>(lroundf(w * 0.35F));
        ov.rect(x + 2, y, bump, 1, col);
        ov.rect(x + w - bump - 3, y + 1, bump + 1, 1, col);
        ov.rect(x + 1, y + 1, bump + 2, 1, col);
        ov.rect(x + 1, y + 2, w - 2, 1, col);
        ov.rect(x, y + 3, w, 1, col);
        ov.rect(x, y + 4, w, 1, shade);
    }
}

auto WeatherLayer::rain(OverlayBuffer& ov, uint8_t level, int n, int cloudBottom) -> void {
    static constexpr std::array<uint8_t, 4> WANT = {0, 10, 22, 40};
    const uint8_t want = WANT.at(level);
    while (_dropCount < want) {
        _drops.at(_dropCount++) = {frand(0, static_cast<float>(n)), frand(static_cast<float>(cloudBottom), static_cast<float>(n - 8)),
                                   frand(0.8F, 1.3F), static_cast<uint8_t>(irand(2, 4))};
    }
    if (_dropCount > want) {
        _dropCount = want;
    }
    const uint8_t head = ov.color(rgb(225, 238, 255));
    const uint8_t tail = ov.color(rgb(170, 195, 235));
    for (uint8_t i = 0; i < _dropCount; i++) {
        Drop& d = _drops.at(i);
        d.y += d.v;
        d.x += 0.075F;
        if (d.y > n - 6) {
            d.y = static_cast<float>(cloudBottom) - frand(0, 6);
            d.x = frand(0, static_cast<float>(n));
        }
        for (uint8_t k = 0; k < d.len; k++) {
            const int yy = static_cast<int>(lroundf(d.y)) - k;
            if (yy >= cloudBottom) {
                ov.set(static_cast<int>(lroundf(d.x)), yy, k == 0 ? head : tail);
            }
        }
    }
}

auto WeatherLayer::snow(OverlayBuffer& ov, uint8_t level, int n, int cloudBottom) -> void {
    static constexpr std::array<uint8_t, 4> WANT = {0, 8, 16, 28};
    const uint8_t want = WANT.at(level);
    while (_flakeCount < want) {
        _flakes.at(_flakeCount++) = {frand(0, static_cast<float>(n)), frand(static_cast<float>(cloudBottom), static_cast<float>(n - 8)),
                                     frand(0.12F, 0.25F), frand(0, 6), irand(0, 9) < 3};
    }
    if (_flakeCount > want) {
        _flakeCount = want;
    }
    const uint8_t white = ov.color(rgb(245, 250, 255));
    const uint8_t soft = ov.color(rgb(225, 235, 250));
    for (uint8_t i = 0; i < _flakeCount; i++) {
        Flake& f = _flakes.at(i);
        f.y += f.v;
        const int x = static_cast<int>(lroundf(f.x + sinf(static_cast<float>(_t) * 0.04F + f.ph) * 1.5F));
        if (f.y > n - 6) {
            f.y = static_cast<float>(cloudBottom) - frand(0, 3);
            f.x = frand(0, static_cast<float>(n));
        }
        if (f.y >= cloudBottom) {
            const int y = static_cast<int>(lroundf(f.y));
            ov.set(x, y, white);
            if (f.big) {
                ov.set(x + 1, y, soft);
            }
        }
    }
}

auto WeatherLayer::snowGround(OverlayBuffer& ov, int n) -> void {
    const uint8_t white = ov.color(rgb(236, 242, 250));
    const uint8_t shade = ov.color(rgb(196, 208, 228));
    ov.rect(0, n - 6, n, 6, white);
    for (int x = 0; x < n; x += 7) {
        ov.set(x + 3, n - 6, shade);
        ov.set(x + 4, n - 7, white);
        ov.set(x + 5, n - 7, white);
    }
    ov.rect(0, n - 1, n, 1, shade);
}

auto WeatherLayer::snowman(OverlayBuffer& ov, int x, int base) -> void {
    const uint8_t white = ov.color(rgb(244, 248, 255));
    const uint8_t shade = ov.color(rgb(200, 212, 232));
    const uint8_t coal = ov.color(rgb(30, 30, 40));
    const uint8_t carrot = ov.color(rgb(255, 140, 40));
    const uint8_t scarf = ov.color(rgb(220, 60, 60));
    const uint8_t wood = ov.color(rgb(110, 80, 50));
    auto disc = [&](int cx, int cy, int r) {
        for (int dy = -r; dy <= r; dy++) {
            const int dx = static_cast<int>(floorf(sqrtf(static_cast<float>(r * r - dy * dy))));
            ov.rect(cx - dx, cy + dy, 2 * dx + 1, 1, white);
            ov.set(cx + dx, cy + dy, shade);
        }
    };
    disc(x, base - 4, 4);
    disc(x, base - 10, 3);
    disc(x, base - 15, 2);
    ov.rect(x - 3, base - 12, 7, 1, scarf);
    ov.rect(x + 2, base - 11, 1, 2, scarf);
    ov.set(x - 1, base - 16, coal);
    ov.set(x + 1, base - 16, coal);
    ov.rect(x + 1, base - 15, 2, 1, carrot);
    ov.set(x, base - 10, coal);
    ov.set(x, base - 8, coal);
    ov.rect(x - 7, base - 11, 4, 1, wood);
    ov.rect(x + 4, base - 11, 4, 1, wood);
    ov.rect(x - 3, base - 18, 7, 1, coal);
    ov.rect(x - 2, base - 21, 5, 3, coal);
}

auto WeatherLayer::fogBand(OverlayBuffer& ov, int n) -> void {
    const uint8_t band = ov.color(rgb(196, 200, 206));
    for (int y = 0; y < 7; y++) {
        const int k = 11 - (y * 11) / 7;  // density thins out downwards
        for (int x = 0; x < n; x++) {
            if (((x * 7 + y * 13) % 11) < k) {
                ov.set(x, y, band);
            }
        }
    }
}

auto WeatherLayer::lighthouse(OverlayBuffer& ov, int n) -> void {
    const int x = 3;
    const int base = n - 6;
    const int h = 22;
    const uint8_t red = ov.color(rgb(200, 60, 60));
    const uint8_t white = ov.color(rgb(235, 235, 240));
    const uint8_t dark = ov.color(rgb(60, 60, 70));
    const uint8_t lamp = ov.color(rgb(255, 230, 120));
    for (int y = 0; y < h; y++) {
        ov.rect(x, base - h + y, 5, 1, ((y / 4) % 2) ? red : white);
    }
    ov.rect(x - 1, base - h - 3, 7, 3, dark);
    ov.rect(x + 1, base - h - 2, 3, 1, lamp);

    // beam sweeping up and down around the horizontal, three translucency steps
    _beam += 0.0125F;
    const float ang = sinf(_beam) * 0.45F;
    const std::array<uint8_t, 3> glow = {ov.color(rgb(255, 240, 180), 100), ov.color(rgb(255, 240, 180), 60),
                                         ov.color(rgb(255, 240, 180), 28)};
    const int lx = x + 5;
    const int ly = base - h - 1;
    for (int d = 1; d < 44; d++) {
        const int cx = lx + d;
        const int cy = ly + static_cast<int>(lroundf(sinf(ang) * d * 0.9F));
        const int half = d / 9;
        const uint8_t g = glow.at(d < 15 ? 0 : (d < 30 ? 1 : 2));
        for (int w = -half; w <= half; w++) {
            ov.set(cx, cy + w, g);
        }
    }
}

auto WeatherLayer::stars(OverlayBuffer& ov, int n) -> void {
    const std::array<uint8_t, 3> tone = {ov.color(rgb(120, 130, 170)), ov.color(rgb(175, 185, 215)),
                                         ov.color(rgb(230, 235, 255))};
    for (const Star& s : _stars) {
        const float b = 0.5F + 0.5F * sinf(static_cast<float>(_t) * 0.035F + s.ph);
        if (b > 0.35F) {
            ov.set(s.x, s.y, tone.at(b > 0.8F ? 2 : (b > 0.55F ? 1 : 0)));
        }
    }
    if (_shootLife <= 0 && _t % 480 == 0) {
        _shootLife = 20;
        _shootX = static_cast<float>(irand(5, 30));
        _shootY = 0;
    }
    if (_shootLife > 0) {
        for (int i = 0; i < 5; i++) {
            ov.set(static_cast<int>(lroundf(_shootX - i * 1.3F)), static_cast<int>(lroundf(_shootY - i * 0.5F)),
                   tone.at(i < 2 ? 2 : (i < 4 ? 1 : 0)));
        }
        _shootX += 1.0F;
        _shootY += 0.4F;
        _shootLife--;
    }
    (void)n;
}

auto WeatherLayer::thunder(uint8_t level) -> void {
    static constexpr std::array<float, 4> STRENGTH = {0.0F, 0.5F, 0.7F, 0.9F};
    if (_flash > 0.0F) {
        _flash = fmaxf(0.0F, _flash - 0.175F);
        _moodDirty = true;
    }
    if (--_nextFlash <= 0) {
        _flash = STRENGTH.at(level);
        _nextFlash = irand(60, 180);
        _moodDirty = true;
    }
}

// ---- per frame ----------------------------------------------------------

auto WeatherLayer::frame(OverlayBuffer& ov, bool night) -> void {
    _t++;
    ov.clear();
    const Weather::Condition c = current();
    if (c.kind != _lastKind) {
        _lastKind = c.kind;
        _moodDirty = true;
        _cloudCount = _dropCount = _flakeCount = 0;
    }
    if (c.kind != Weather::Kind::Thunder && _flash > 0.0F) {
        _flash = 0.0F;
        _moodDirty = true;
    }

    const int n = ov.width();
    switch (c.kind) {
        case Weather::Kind::Clear:
            if (night) {
                stars(ov, n);
            }
            break;
        case Weather::Kind::Cloudy:
            clouds(ov, c.kind, n);
            break;
        case Weather::Kind::Fog:
            fogBand(ov, n);
            lighthouse(ov, n);
            break;
        case Weather::Kind::Rain:
            clouds(ov, c.kind, n);
            rain(ov, c.level, n, 7);
            break;
        case Weather::Kind::Snow:
            clouds(ov, c.kind, n);
            snowGround(ov, n);
            snowman(ov, 48, n - 6);
            snow(ov, c.level, n, 7);
            break;
        case Weather::Kind::Thunder:
            thunder(c.level);
            clouds(ov, c.kind, n);
            rain(ov, c.level, n, 7);
            break;
    }
}
