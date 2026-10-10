// SPDX-License-Identifier: GPL-3.0-or-later
#include "weather/Messenger.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config/ConfigManager.h"
#include "display/ColorProfile.h"
#include "weather/WeatherService.h"

#include <ctime>

extern ConfigManager configManager;

namespace {

// 3x5 digits, minus and degree sign; one byte per row, bit 2 is the left column
constexpr uint8_t FONT_H = 5;
constexpr uint8_t GLYPH_W = 3;
constexpr uint8_t GLYPH_MINUS = 10;
constexpr uint8_t GLYPH_DEGREE = 11;
const uint8_t FONT[12][FONT_H] PROGMEM = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 7, 0, 0}, {7, 5, 7, 0, 0},
};

auto glyphIndex(char c) -> uint8_t {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    return c == '-' ? GLYPH_MINUS : GLYPH_DEGREE;
}

constexpr int FIRST_WAIT_FRAMES = 48;  // 2 s after boot
constexpr int MIN_WAIT_FRAMES = 24;    // 1 s
constexpr int MAX_WAIT_FRAMES = 48;    // 2 s; with walk-in and walk-out the gap stays under 4 s

const char* const KIND_NAMES[Messenger::KIND_COUNT] = {"crab", "sub", "puffer", "chest", "bottle", "octo", "hook", "neon", "parade"};

// ---- drawing context: the overlay of the current frame, a clip row and the night dim ----
OverlayBuffer* g_ov = nullptr;
int g_clip = 1000;  // rows at or below this are not drawn (figures rising out of the sand)
bool g_dim = false;
bool g_night = false;
bool g_dry = false;  // register colors only, draw nothing (palette reservation)

auto rgb(uint8_t r, uint8_t g, uint8_t b, bool dim) -> uint16_t {
    float fr = r, fg = g, fb = b;
    if (dim) {  // night: 30 % towards the deep water color, like the resting fish
        fr = fr * 0.7F + 15 * 0.3F;
        fg = fg * 0.7F + 20 * 0.3F;
        fb = fb * 0.7F + 45 * 0.3F;
    }
    ColorProfile::apply(fr, fg, fb);
    return static_cast<uint16_t>(((static_cast<uint16_t>(fr) & 0xF8) << 8) | ((static_cast<uint16_t>(fg) & 0xFC) << 3) |
                                 (static_cast<uint16_t>(fb) >> 3));
}
auto C(uint8_t r, uint8_t g, uint8_t b) -> uint8_t { return g_ov->color(rgb(r, g, b, g_dim)); }
auto CA(uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) -> uint8_t { return g_ov->color(rgb(r, g, b, false), alpha); }

auto put(int x, int y, uint8_t idx) -> void {
    if (!g_dry && y < g_clip) {
        g_ov->set(x, y, idx);
    }
}
auto fill(int x, int y, int w, int h, uint8_t idx) -> void {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            put(x + i, y + j, idx);
        }
    }
}
auto disc(int cx, int cy, int r, uint8_t idx) -> void {
    for (int dy = -r; dy <= r; dy++) {
        const int dx = static_cast<int>(floorf(sqrtf(static_cast<float>(r * r - dy * dy))));
        fill(cx - dx, cy + dy, 2 * dx + 1, 1, idx);
    }
}
auto irand(int lo, int hi) -> int { return static_cast<int>(random(lo, hi + 1)); }
auto rndf(float v) -> int { return static_cast<int>(lroundf(v)); }

// text in the 3x5 font, optionally scaled, with its own color index
auto text(const char* s, int x, int y, uint8_t idx, int scale = 1) -> void {
    for (const char* c = s; *c != '\0'; c++) {
        const uint8_t gi = glyphIndex(*c);
        for (uint8_t r = 0; r < FONT_H; r++) {
            const uint8_t bits = pgm_read_byte(&FONT[gi][r]);
            for (uint8_t col = 0; col < GLYPH_W; col++) {
                if ((bits >> (GLYPH_W - 1 - col)) & 1) {
                    fill(x + col * scale, y + r * scale, scale, scale, idx);
                }
            }
        }
        x += (GLYPH_W + 1) * scale;
    }
}
auto textWidthOf(const char* s) -> int { return static_cast<int>(strlen(s)) * (GLYPH_W + 1) - 1; }

// a cream board with a wooden frame around a text, bw = tw + 4, bh = FONT_H + 4
auto board(int left, int top, const char* s, uint8_t ink) -> void {
    const int bw = textWidthOf(s) + 4;
    const int bh = FONT_H + 4;
    fill(left, top, bw, bh, C(110, 80, 50));
    fill(left + 1, top + 1, bw - 2, bh - 2, C(245, 238, 215));
    text(s, left + 2, top + 2, ink);
}

// the scene fish shape, also used by the puffer before it inflates
auto fish(int x, int y, int d, uint8_t body, uint8_t eye) -> void {
    fill(x - 3, y - 1, 6, 3, body);
    fill(x - 2, y - 2, 4, 1, body);
    fill(x - 2, y + 2, 4, 1, body);
    fill(x - 4 * d - (d > 0 ? 1 : 0), y - 2, 2, 5, body);
    put(x + 2 * d, y - 1, eye);
}

// mirrored helpers: local x grows in the walking direction d
auto mx(int x, int d, int lx) -> int { return d > 0 ? x + lx : x - lx; }
auto mrect(int x, int y, int d, int lx, int ly, int w, int h, uint8_t idx) -> void {
    fill(d > 0 ? x + lx : x - lx - w + 1, y + ly, w, h, idx);
}

const int8_t SPIKES[8][2] = {{10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}};  // unit circle x10
const uint8_t FLICKER[13] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 1};

// crab gaits: scuttle in bursts, stand still and snap the claws, stroll
constexpr uint8_t GAIT_PAUSE = 0;
constexpr uint8_t GAIT_STROLL = 1;
constexpr uint8_t GAIT_SCUTTLE = 2;

}  // namespace

int8_t Messenger::_forced = -1;
Messenger::Mode Messenger::_mode = Messenger::Mode::Random;
Messenger::Kind Messenger::_fixed = Messenger::Kind::Crab;
Messenger::Kind Messenger::_kind = Messenger::Kind::Crab;
uint8_t Messenger::_demoIndex = 0;
int32_t Messenger::_dayKey = -1;
uint8_t Messenger::_rerollSalt = 0;
std::array<Messenger::Kind, Messenger::DAILY_COUNT> Messenger::_trio{};
int8_t Messenger::_lastPick = -1;
bool Messenger::_active = false;
uint8_t Messenger::_phase = 0;
int16_t Messenger::_timer = 0;
uint32_t Messenger::_t = 0;
int Messenger::_wait = FIRST_WAIT_FRAMES;
float Messenger::_x = 0;
float Messenger::_y = 0;
float Messenger::_a = 0;
float Messenger::_b = 0;
float Messenger::_c = 0;
int8_t Messenger::_look = 1;
int16_t Messenger::_tx = 0;
int8_t Messenger::_dir = 1;
std::array<Messenger::Bubble, 7> Messenger::_bub{};
uint8_t Messenger::_bubN = 0;
float Messenger::_override = NAN;
float Messenger::_shown = NAN;
char Messenger::_text[Messenger::TEXT_MAX] = "";
uint8_t Messenger::_textWidth = 0;

auto Messenger::begin() -> void {
    _active = false;
    _wait = FIRST_WAIT_FRAMES;
}

auto Messenger::enabled() -> bool { return _forced < 0 ? configManager.getMessengerMode() : _forced == 1; }

auto Messenger::setEnabled(bool on, bool persist) -> void {
    if (persist) {
        configManager.setMessengerMode(on);
        _forced = -1;
    } else {
        _forced = on ? 1 : 0;
    }
    if (!on) {
        _active = false;
        _shown = NAN;
    }
}

auto Messenger::setMode(Mode mode, Kind fixed) -> void {
    _mode = mode;
    _fixed = fixed;
    if (mode == Mode::Demo) {
        _demoIndex = 0;
    }
}

auto Messenger::modeName() -> const char* {
    switch (_mode) {
        case Mode::Demo:
            return "demo";
        case Mode::Fixed:
            return kindName(_fixed);
        default:
            return "random";
    }
}

auto Messenger::currentName() -> const char* { return _active ? kindName(_kind) : ""; }

auto Messenger::kindName(Kind kind) -> const char* { return KIND_NAMES[static_cast<uint8_t>(kind)]; }

auto Messenger::parseKind(const char* s, Kind& out) -> bool {
    for (uint8_t i = 0; i < KIND_COUNT; i++) {
        if (strcmp(s, KIND_NAMES[i]) == 0) {
            out = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

// A day for the figures runs from 4:00 to 4:00 local time
auto Messenger::dayKey() -> int32_t {
    constexpr time_t EPOCH_SANE = 1600000000;  // 2020-09-13, anything earlier is the unset clock
    time_t now = time(nullptr);
    if (now < EPOCH_SANE) {
        return -1;
    }
    now -= 4 * 3600;
    struct tm local{};
    localtime_r(&now, &local);
    return static_cast<int32_t>(local.tm_year) * 1000 + local.tm_yday;
}

// Three distinct kinds from a hash of the day and the chip id: looks random, survives a reboot
auto Messenger::drawTrio(int32_t key) -> void {
    uint32_t h = static_cast<uint32_t>(key) * 2654435761UL ^ ESP.getChipId() ^ (static_cast<uint32_t>(_rerollSalt) << 24);
    auto next = [&h]() {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        return h;
    };
    std::array<uint8_t, KIND_COUNT> pool{};
    for (uint8_t i = 0; i < KIND_COUNT; i++) {
        pool.at(i) = i;
    }
    for (uint8_t i = 0; i < DAILY_COUNT; i++) {  // partial Fisher-Yates
        const uint8_t j = static_cast<uint8_t>(i + next() % (KIND_COUNT - i));
        std::swap(pool.at(i), pool.at(j));
        _trio.at(i) = static_cast<Kind>(pool.at(i));
    }
    _dayKey = key;
    _lastPick = -1;
}

auto Messenger::today(std::array<Kind, DAILY_COUNT>& out) -> bool {
    const int32_t key = dayKey();
    if (key < 0) {
        return false;
    }
    if (key != _dayKey) {
        _rerollSalt = 0;
        drawTrio(key);
    }
    out = _trio;
    return true;
}

auto Messenger::reroll() -> void {
    const int32_t key = dayKey();
    if (key >= 0) {
        _rerollSalt++;
        drawTrio(key);
    }
}

auto Messenger::pickDaily() -> Kind {
    std::array<Kind, DAILY_COUNT> trio{};
    if (!today(trio)) {
        return static_cast<Kind>(irand(0, KIND_COUNT - 1));  // no clock yet: all nine
    }
    int8_t i = static_cast<int8_t>(irand(0, DAILY_COUNT - 1));
    if (i == _lastPick) {
        i = static_cast<int8_t>((i + 1) % DAILY_COUNT);
    }
    _lastPick = i;
    return trio.at(static_cast<size_t>(i));
}

auto Messenger::triggerNow() -> void {
    _active = false;
    _wait = 0;
}

auto Messenger::setOverride(float celsius) -> void { _override = celsius; }

auto Messenger::overrideActive() -> bool { return !std::isnan(_override); }

auto Messenger::shownTemperature() -> float { return _active ? _shown : NAN; }

// Picks the value to show and renders it into _text; false when there is no temperature yet
auto Messenger::prepareText() -> bool {
    float t = _override;
    if (std::isnan(t)) {
        const WeatherData& w = WeatherService::data();
        if (!w.valid) {
            return false;
        }
        t = w.tempCurrent;
    }
    const int v = static_cast<int>(lroundf(fmaxf(-99.0F, fminf(99.0F, t))));
    _shown = static_cast<float>(v);  // the rounded value, so the ink color matches the digits
    snprintf(_text, sizeof(_text), "%d*", v);  // '*' is the degree sign
    _textWidth = static_cast<uint8_t>(textWidthOf(_text));
    return true;
}

auto Messenger::draw(OverlayBuffer& ov, bool night, bool snow) -> void {
    if (!_active) {
        return;
    }
    g_ov = &ov;
    g_dim = night && !snow;
    g_night = night;
    g_dry = false;
    render(snow ? ov.width() - 7 : ov.width() - 6);
}

auto Messenger::advance(OverlayBuffer& ov, bool night, bool snow) -> void {
    if (!enabled()) {
        _active = false;
        return;
    }
    const int n = ov.width();
    const int ground = snow ? n - 7 : n - 6;  // the row the ground props stand on
    g_ov = &ov;
    g_dim = night && !snow;
    g_night = night;
    if (!_active) {
        if (_wait > 0) {
            _wait--;
            return;
        }
        if (!prepareText()) {
            return;  // no temperature yet, try again next frame
        }
        switch (_mode) {
            case Mode::Demo:
                _kind = static_cast<Kind>(_demoIndex % KIND_COUNT);
                _demoIndex = static_cast<uint8_t>((_demoIndex + 1) % KIND_COUNT);
                break;
            case Mode::Fixed:
                _kind = _fixed;
                break;
            default:
                _kind = pickDaily();
                break;
        }
        _active = true;
        _t = 0;
        _phase = 0;
        _timer = 0;
        _bubN = 0;
        _dir = irand(0, 1) ? 1 : -1;
        start(n, ground);
    }
    _t++;
    if (!step(n, ground)) {
        _active = false;
        _wait = irand(MIN_WAIT_FRAMES, MAX_WAIT_FRAMES);
        return;
    }
    g_dry = true;  // same calls as the real draw, so the palette holds this frame's colors in this order
    render(ground);
    g_dry = false;
}

// ---- per messenger: start() sets the pose, step() advances one frame (24 fps), draw() renders the state ----

auto Messenger::start(int n, int ground) -> void {
    const int tw = _textWidth;
    switch (_kind) {
        case Kind::Crab: {
            const int bw = tw + 4;
            const int lead = (8 > 5 + bw - bw / 2 ? 8 : 5 + bw - bw / 2) + 1;
            _x = static_cast<float>(_dir > 0 ? -lead : n - 1 + lead);
            _phase = GAIT_STROLL;
            _timer = static_cast<int16_t>(irand(24, 72));
            _a = 0;  // steps taken
            _b = 0;  // claw snap countdown
            break;
        }
        case Kind::Sub: {
            const int hw = tw + 6 > 16 ? tw + 6 : 16;
            _a = static_cast<float>(hw);
            _b = 0.175F;  // speed
            _c = 0;       // periscope extension
            _look = 1;
            _y = static_cast<float>(irand(14, 30));
            _x = static_cast<float>(_dir > 0 ? -hw : n + hw);
            _tx = static_cast<int16_t>(irand(18, 42));  // where it stops to raise the periscope
            break;
        }
        case Kind::Puffer:
            _b = static_cast<float>((tw + 5) / 2 > 8 ? (tw + 5) / 2 : 8);  // full radius, 8 keeps the eye clear of the digits
            _a = 0;                                                        // current radius
            _y = static_cast<float>(irand(18, 34));
            _tx = static_cast<int16_t>(_dir > 0 ? irand(16, 26) : n - irand(16, 26));  // where it takes fright
            _x = static_cast<float>(_dir > 0 ? -6 : n + 6);
            break;
        case Kind::Chest:
            _x = static_cast<float>(irand(14, 46));
            _a = 10;  // rise: rows still in the sand
            _b = 0;   // lift of the gold plate
            break;
        case Kind::Bottle:
            _x = static_cast<float>(irand(8, 32));
            _y = -12;
            _a = 0;  // unrolled width of the note
            break;
        case Kind::Octo:
            _tx = static_cast<int16_t>(irand(24, 36));
            _x = static_cast<float>(_dir > 0 ? -8 : n + 8);
            _y = static_cast<float>(ground - 22);  // centre row of the ink digits
            break;
        case Kind::Hook:
            _tx = static_cast<int16_t>(irand(16, n - 16));
            _a = static_cast<float>(irand(18, 34));  // hook depth
            _x = static_cast<float>(_dir > 0 ? -10 : n + 10);  // the boat
            _y = 3;                                           // the hook
            break;
        case Kind::Neon:
            _x = static_cast<float>(irand(16, 44));
            _a = 16;  // rise
            break;
        case Kind::Parade:
            _x = static_cast<float>(_dir > 0 ? -7 : n + 7);
            _phase = GAIT_STROLL;
            _timer = static_cast<int16_t>(irand(24, 72));
            _a = 0;
            _b = 0;
            break;
    }
}

auto Messenger::step(int n, int ground) -> bool {
    const int d = _dir;
    const int tw = _textWidth;
    switch (_kind) {
        case Kind::Crab: {
            gaitStep();
            const int cx = rndf(_x);
            return d > 0 ? (cx - 8 < n) : (cx + 8 >= 0);
        }
        case Kind::Sub: {
            if (_phase == 0) {
                _x += _b * d;
                if ((d > 0 && _x >= _tx) || (d < 0 && _x <= _tx)) {
                    _phase = 1;
                }
            } else if (_phase == 1) {  // coasting to a stop
                _b = fmaxf(0.0F, _b - 0.00875F);
                _x += _b * d;
                if (_b <= 0.0F) {
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {  // periscope up
                if (++_timer % 4 == 0) {
                    _c += 1;
                }
                if (_c >= 6) {
                    _phase = 3;
                    _timer = 0;
                }
            } else if (_phase == 3) {  // scanning
                if (++_timer % 28 == 0) {
                    _look = static_cast<int8_t>(-_look);
                }
                if (_timer >= 112) {
                    _phase = 4;
                    _timer = 0;
                }
            } else if (_phase == 4) {  // periscope down
                if (++_timer % 4 == 0) {
                    _c -= 1;
                }
                if (_c <= 0) {
                    _c = 0;
                    _phase = 5;
                }
            } else {  // picking up speed again
                _b = fminf(0.175F, _b + 0.005F);
                _x += _b * d;
            }
            const int cx = rndf(_x);
            const int cy = rndf(_y + sinf(_t * 0.06F));
            if (_b > 0.025F && _t % 8 == 0 && _bubN < _bub.size()) {
                _bub.at(_bubN++) = {static_cast<float>(mx(cx, d, -3)), static_cast<float>(cy + 3), 56};
            }
            for (uint8_t i = 0; i < _bubN; i++) {
                Bubble& b = _bub.at(i);
                b.y -= 0.175F;
                b.x -= 0.075F * d;
                if (--b.life == 0) {
                    _bub.at(i) = _bub.at(_bubN - 1);
                    _bubN--;
                    i--;
                }
            }
            return d > 0 ? (cx - 8 < n) : (cx + 8 >= 0);
        }
        case Kind::Puffer: {
            const int rMax = static_cast<int>(_b);
            if (_phase == 0) {
                _x += 0.2F * d;
                if ((d > 0 && _x >= _tx) || (d < 0 && _x <= _tx)) {
                    _phase = 1;
                }
            } else if (_phase == 1) {
                if (_t % 2 == 0 && ++_a >= rMax) {
                    _phase = 2;
                }
            } else {
                _x += 0.1F * d;  // drifts across as a ball and stays inflated until it is out of view
            }
            const int cx = rndf(_x);
            return _phase < 2 || (d > 0 ? (cx - rMax - 3 < n) : (cx + rMax + 3 >= 0));
        }
        case Kind::Chest:
            _timer++;
            if (_phase == 0) {
                if (--_a <= 0) {
                    _a = 0;
                    _phase = 1;
                    _timer = 0;
                }
            } else if (_phase == 1) {
                if (_timer >= 12) {
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {
                if (_timer % 2 == 0 && _b < 6) {
                    _b++;
                }
                if (_timer >= 240) {
                    _phase = 3;
                    _timer = 0;
                }
            } else if (_phase == 3) {
                if (_timer % 2 == 0 && _b > 0) {
                    _b--;
                }
                if (_timer >= 16) {
                    _phase = 4;
                    _timer = 0;
                }
            } else {
                _a++;
                if (_a >= 12) {
                    return false;
                }
            }
            return true;
        case Kind::Bottle:
            _timer++;
            if (_phase == 0) {
                _y += 0.175F;
                if (_y >= ground - 10) {
                    _y = static_cast<float>(ground - 10);
                    _phase = 1;
                    _timer = 0;
                }
            } else if (_phase == 1) {
                if (_timer % 2 == 0) {
                    _a++;
                }
                if (_a >= tw + 4) {
                    _a = static_cast<float>(tw + 4);
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {
                if (_timer >= 240) {
                    _phase = 3;
                    _timer = 0;
                }
            } else if (_phase == 3) {
                if (_timer % 2 == 0) {
                    _a--;
                }
                if (_a <= 0) {
                    _a = 0;
                    _phase = 4;
                    _timer = 0;
                }
            } else {
                _y -= 0.2F;
                if (_y < -14) {
                    return false;
                }
            }
            return true;
        case Kind::Octo: {
            if (_phase == 0) {
                _x += 0.125F * d;
                if ((d > 0 && _x >= _tx) || (d < 0 && _x <= _tx)) {
                    _x = static_cast<float>(_tx);
                    _phase = 1;
                    _timer = 0;
                }
            } else if (_phase == 1) {
                if (++_timer > 20) {
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {
                if (++_timer > 16) {
                    _phase = 3;
                    _timer = 0;
                }
            } else if (_phase == 3) {
                if (++_timer > 240) {
                    _phase = 4;
                    _timer = 0;
                }
            } else if (_phase == 4) {
                if (++_timer > 24) {
                    _phase = 5;
                    _timer = 0;
                }
            } else {
                _x += 0.15F * d;
            }
            const int cx = rndf(_x);
            return _phase < 5 || (d > 0 ? (cx - 8 < n) : (cx + 8 >= 0));
        }
        case Kind::Hook: {
            if (_phase == 0) {
                _x += 0.25F * d;
                if ((d > 0 && _x >= _tx) || (d < 0 && _x <= _tx)) {
                    _x = static_cast<float>(_tx);
                    _phase = 1;
                }
            } else if (_phase == 1) {
                _y += 0.3F;
                if (_y >= _a) {
                    _y = _a;
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {
                if (++_timer > 264) {
                    _phase = 3;
                }
            } else if (_phase == 3) {
                _y -= 1.5F;
                if (_y <= 3) {
                    _y = 3;
                    _phase = 4;
                }
            } else {
                _x += 0.3F * d;
            }
            const int cx = rndf(_x);
            return _phase < 4 || (d > 0 ? (cx - 12 < n) : (cx + 12 >= 0));
        }
        case Kind::Neon:
            _timer++;
            if (_phase == 0) {
                if (--_a <= 0) {
                    _a = 0;
                    _phase = 1;
                    _timer = 0;
                }
            } else if (_phase == 1) {
                if (_timer >= 26) {
                    _phase = 2;
                    _timer = 0;
                }
            } else if (_phase == 2) {
                if (_timer >= 264) {
                    _phase = 3;
                    _timer = 0;
                }
            } else if (_phase == 3) {
                if (_timer >= 12) {
                    _phase = 4;
                    _timer = 0;
                }
            } else {
                _a++;
                if (_a >= 18) {
                    return false;
                }
            }
            return true;
        case Kind::Parade: {
            gaitStep();
            const int len = static_cast<int>(strlen(_text));
            const int tail = rndf(_x) - d * (12 + (len - 1) * 9);
            return d > 0 ? (tail - 5 < n) : (tail + 5 >= 0);
        }
    }
    return false;
}

auto Messenger::gaitStep() -> void {
    const int d = _dir;
    if (--_timer <= 0) {
        const int r = irand(0, 9);
        if (_phase == GAIT_PAUSE) {
            _phase = r < 6 ? GAIT_SCUTTLE : GAIT_STROLL;
        } else if (_phase == GAIT_STROLL) {
            _phase = r < 5 ? GAIT_PAUSE : GAIT_SCUTTLE;
        } else {
            _phase = r < 7 ? GAIT_PAUSE : GAIT_STROLL;
        }
        _timer = static_cast<int16_t>(_phase == GAIT_PAUSE ? irand(16, 60) : _phase == GAIT_STROLL ? irand(24, 72) : irand(16, 40));
    }
    const int rate = _phase == GAIT_PAUSE ? 0 : (_phase == GAIT_STROLL ? 6 : 2);  // frames per pixel
    if (rate != 0 && _t % rate == 0) {
        _x += static_cast<float>(d);
        _a += 1;  // steps taken, drives the legs
    }
    if (_phase == GAIT_PAUSE && _t % 24 == 0) {
        _b = 8;  // the claws snap while standing
    }
    if (_b > 0) {
        _b -= 1;
    }
}

auto Messenger::render(int ground) -> void {
    const int d = _dir;
    const int tw = _textWidth;
    const int base = ground;
    const float tv = _shown;
    const auto inkColor = [tv]() { return tv <= 0.0F ? C(30, 80, 210) : (tv >= 25.0F ? C(205, 50, 30) : C(25, 25, 35)); };

    switch (_kind) {
        case Kind::Crab: {
            // Under snow the snowman and the crab share the palette: eye, wood and shell use the snowman's
            // coal, stick and scarf colors.
            const int cx = rndf(_x);
            const uint8_t shell = C(220, 60, 60), dark = C(120, 30, 25), eye = C(30, 30, 40), wood = C(110, 80, 50);
            const int stepk = static_cast<int>(_a) % 2;
            const int bob = (_phase == GAIT_SCUTTLE && stepk) ? -1 : 0;                 // bounces while scuttling
            const int wig = _phase == GAIT_PAUSE ? rndf(sinf(_t * 0.2F)) : 0;           // eye stalks sway while standing
            const int open = _b > 3 ? 1 : 0;                                            // upper pincer lifts
            fill(cx - 4, base - 4 + bob, 9, 3, shell);
            fill(cx - 3, base - 5 + bob, 7, 1, shell);
            fill(cx - 4, base - 1 + bob, 9, 1, dark);
            put(cx - 2 + wig, base - 6 + bob, shell);
            put(cx + 2 + wig, base - 6 + bob, shell);
            put(cx - 2 + wig, base - 7 + bob, eye);
            put(cx + 2 + wig, base - 7 + bob, eye);
            for (int i = 0; i < 3; i++) {
                const int ly = _phase == GAIT_PAUSE ? 0 : ((stepk == i % 2) ? 0 : -1);
                put(cx - 5 - i, base - 1 + ly, dark);
                put(cx - 6 - i, base + ly, dark);
                put(cx + 5 + i, base - 1 + ly, dark);
                put(cx + 6 + i, base + ly, dark);
            }
            fill(cx - 6, base - 4 + bob - open, 2, 1, shell);
            fill(cx - 6, base - 3 + bob, 2, 1, shell);
            fill(cx + 5, base - 4 + bob - open, 2, 1, shell);
            fill(cx + 5, base - 3 + bob, 2, 1, shell);
            const int poleX = cx + 5 * d;
            fill(poleX, base - 8 + bob, 1, 4, wood);
            const int bw = tw + 4;
            board(poleX - bw / 2, base - 8 + bob - (FONT_H + 4), _text, inkColor());
            break;
        }
        case Kind::Sub: {
            const int cx = rndf(_x);
            const int cy = rndf(_y + sinf(_t * 0.06F));
            const int hw = static_cast<int>(_a);
            const int hh = FONT_H + 2;
            const uint8_t hull = C(240, 200, 50), shade = C(190, 150, 30), dark = C(60, 45, 20), glass = C(150, 220, 255);
            for (int r = 0; r < hh; r++) {
                const int inset = (r == 0 || r == hh - 1) ? 2 : (r == 1 || r == hh - 2) ? 1 : 0;
                mrect(cx, cy, d, inset, r, hw - 2 * inset, 1, r == hh - 1 ? shade : hull);
            }
            mrect(cx, cy, d, hw - 4, 2, 2, 2, glass);           // porthole at the bow
            mrect(cx, cy, d, hw / 2 - 1, -3, 4, 3, hull);        // tower
            const int ext = static_cast<int>(_c);
            mrect(cx, cy, d, hw / 2 + 1, -4 - ext, 1, 1 + ext, dark);  // periscope: a stub while cruising, 6 px when stopped
            if (ext > 0) {
                mrect(cx, cy, d, _look > 0 ? hw / 2 + 1 : hw / 2, -4 - ext, 2, 1, dark);  // the head looks forward or back
            }
            if (_b > 0.025F && (_t / 6) % 2) {
                mrect(cx, cy, d, -2, hh / 2 - 1, 1, 3, dark);  // propeller
            } else {
                put(mx(cx, d, -2), cy + hh / 2, dark);
            }
            text(_text, d > 0 ? cx + 2 : cx - 1 - tw, cy + 1, inkColor());
            for (uint8_t i = 0; i < _bubN; i++) {
                put(rndf(_bub.at(i).x), rndf(_bub.at(i).y), glass);
            }
            break;
        }
        case Kind::Puffer: {
            const int cx = rndf(_x);
            const int cy = rndf(_y + sinf(_t * 0.075F));
            const int r = static_cast<int>(_a);
            const int rMax = static_cast<int>(_b);
            const uint8_t body = C(235, 200, 70), belly = C(250, 240, 190), dark = C(70, 50, 30);
            if (r <= 2) {
                fish(cx, cy, d, body, dark);
                put(cx - 2, cy - 3, dark);
                put(cx, cy - 3, dark);
                put(cx + 2, cy - 3, dark);  // spines folded on the back
            } else {
                for (int dy = -r; dy <= r; dy++) {
                    const int dx = static_cast<int>(floorf(sqrtf(static_cast<float>(r * r - dy * dy))));
                    fill(cx - dx, cy + dy, 2 * dx + 1, 1, dy * 3 > r ? belly : body);
                }
                mrect(cx, cy, d, -r - 2, (_phase == 2 && (_t / 8) % 2) ? 0 : -1, 2, 3, body);  // tail, wagging while it drifts
                mrect(cx, cy, d, r - 4, -r + 3, 2, 2, belly);  // eye
                put(mx(cx, d, r - 3), cy - r + 3, dark);
                mrect(cx, cy, d, r - 1, 1, 1, 2, dark);  // mouth
                if (r >= rMax - 1) {
                    for (const auto& s : SPIKES) {
                        put(cx + rndf(s[0] * (r + 1.5F) / 10.0F), cy + rndf(s[1] * (r + 1.5F) / 10.0F), dark);
                    }
                    text(_text, cx - tw / 2, cy - FONT_H / 2, inkColor());
                }
            }
            break;
        }
        case Kind::Chest: {
            const int cx = rndf(_x);
            const int b = base + static_cast<int>(_a);
            g_clip = ground + 1;
            const uint8_t wood = C(130, 80, 40), dark = C(90, 55, 30), gold = C(230, 190, 60);
            fill(cx - 6, b - 6, 13, 6, wood);
            fill(cx - 6, b - 1, 13, 1, dark);
            fill(cx - 4, b - 6, 1, 6, gold);
            fill(cx + 4, b - 6, 1, 6, gold);
            fill(cx - 1, b - 4, 3, 2, gold);  // bands and lock
            const int open = _phase == 1 ? (_timer / 4 < 3 ? _timer / 4 : 3) : _phase == 2 ? 3 : _phase == 3 ? (3 - _timer / 4 > 0 ? 3 - _timer / 4 : 0) : 0;
            const int ly = b - 7 - open;
            if (open > 0) {
                fill(cx - 5, b - 7 - open + 1, 11, open, CA(255, 230, 120, 153));  // the glow between lid and body
            }
            fill(cx - 6, ly - 2, 13, 3, dark);
            fill(cx - 5, ly - 3, 11, 1, dark);
            fill(cx - 4, ly - 2, 1, 3, gold);
            fill(cx + 4, ly - 2, 1, 3, gold);
            const int lift = static_cast<int>(_b);
            if (lift > 0 || _phase == 2) {
                const int bw = tw + 4;
                const int bh = FONT_H + 2;
                const int top = ly - 3 - bh - lift;
                const int left = cx - bw / 2;
                fill(left, top, bw, bh, gold);
                fill(left, top + bh - 1, bw, 1, C(190, 140, 40));
                text(_text, left + 2, top + 1, inkColor());
            }
            g_clip = 1000;
            break;
        }
        case Kind::Bottle: {
            const bool drifting = _phase == 0 || _phase == 4;
            const int bx = rndf(_x + (drifting ? sinf(_t * 0.075F) * 1.5F : 0.0F));
            const int by = rndf(_y);
            const uint8_t glass = CA(120, 200, 160, 191), cork = C(160, 110, 60), hi = C(220, 250, 235), paper = C(245, 238, 215);
            fill(bx, by + 3, 5, 8, glass);  // body
            fill(bx + 1, by, 3, 3, glass);  // neck
            fill(bx + 1, by - 1, 3, 1, cork);
            put(bx + 1, by + 4, hi);
            put(bx + 1, by + 5, hi);
            if (_phase <= 1 || _phase >= 3) {
                fill(bx + 2, by + 5, 1, 4, paper);  // the note inside
            }
            const int scroll = static_cast<int>(_a);
            if (scroll > 0) {
                const int bh = FONT_H + 2;
                const int left = bx + 6;
                const int top = ground - bh + 1;
                fill(left, top, scroll, bh, paper);
                fill(left + scroll - 1, top, 1, bh, C(200, 185, 150));
                if (scroll >= tw + 4) {
                    text(_text, left + 2, top + 1, inkColor());
                }
            }
            if (_phase == 4 && _t % 10 < 6) {
                put(bx + 2, by - 3 - static_cast<int>((_t % 10) / 2), hi);  // bubbles above the cork
            }
            break;
        }
        case Kind::Octo: {
            const int cx = rndf(_x);
            const bool moving = _phase == 0 || _phase == 5;
            const uint8_t body = C(150, 80, 170), light = C(190, 120, 200), white = C(240, 240, 250), pupil = C(20, 15, 30);
            const uint8_t ir = g_night ? 185 : 20, ig = g_night ? 170 : 15, ib = g_night ? 235 : 40;  // dark ink by day, pale violet at night
            for (int r = 0; r < 7; r++) {
                static const int8_t INSET[7] = {3, 2, 1, 0, 0, 0, 0};
                fill(cx - 4 + INSET[r], base - 10 + r, 9 - 2 * INSET[r], 1, body);
            }
            put(cx - 2, base - 7, white);
            put(cx + 2, base - 7, white);
            put(cx - 2 + (d > 0 ? 1 : 0), base - 6, pupil);
            put(cx + 2 + (d > 0 ? 1 : 0), base - 6, pupil);
            for (int i = 0; i < 4; i++) {
                static const int8_t OX[4] = {-5, -2, 1, 4};
                for (int k = 0; k < 3; k++) {
                    const float sway = sinf(_t * (moving ? 0.25F : 0.1F) + i * 1.3F + k * 0.8F) * (k == 2 ? 1.5F : 0.6F);
                    put(cx + OX[i] + rndf(sway), base - 3 + k, k == 2 ? light : body);
                }
            }
            const int textCx = _tx - 8 * d;
            const int textCy = static_cast<int>(_y);
            const int left = textCx - tw;  // the scaled digits are 2 * tw wide
            const int top = textCy - FONT_H;
            if (_phase == 1) {  // the ink cloud leaves the siphon and drifts to where the digits form
                const float k = _timer / 20.0F;
                const int ox = cx - 4 * d;
                const int oy = base - 8;
                const int ccx = rndf(ox + (textCx - ox) * k);
                const int ccy = rndf(oy + (textCy - oy) * k);
                const int r = 1 + rndf(k * 7);
                const uint8_t inner = CA(ir, ig, ib, 166), outer = CA(ir, ig, ib, 115);
                for (int dy = -r; dy <= r; dy++) {
                    const int dx = static_cast<int>(floorf(sqrtf(static_cast<float>(r * r - dy * dy))));
                    for (int c = -dx; c <= dx; c++) {
                        put(ccx + c, ccy + dy, (abs(c) + abs(dy)) * 2 > r ? outer : inner);
                    }
                }
            } else if (_phase == 2) {
                const float k = _timer / 16.0F;
                const int r = rndf(7 * (1 - k));
                disc(textCx, textCy, r, CA(ir, ig, ib, 153));
                text(_text, left, top, CA(ir, ig, ib, k < 0.5F ? 102 : 217), 2);
            } else if (_phase == 3) {
                const uint8_t soft = CA(ir, ig, ib, 38);
                text(_text, left - 1, top, soft, 2);
                text(_text, left + 1, top, soft, 2);
                text(_text, left, top, CA(ir, ig, ib, 217), 2);
            } else if (_phase == 4) {
                const float k = _timer / 24.0F;
                const uint8_t a = k < 0.3F ? 217 : (k < 0.6F ? 128 : 51);
                text(_text, left + rndf(sinf(_t * 0.35F) * k * 2), top - rndf(k * 6), CA(ir, ig, ib, a), 2);
            }
            break;
        }
        case Kind::Hook: {
            const int bob = rndf(sinf(_t * 0.075F));
            const int cx = rndf(_x);
            const uint8_t hull = C(12, 22, 40), ripple = C(205, 232, 240);
            static const int8_t HALF[3] = {8, 6, 3};
            for (int r = 0; r < 3; r++) {
                fill(cx - HALF[r] + (r == 2 ? d : 0), r + bob, 2 * HALF[r] + 1, 1, hull);  // the boat seen from below
            }
            put(cx - 7 * d, 2 + bob, hull);
            put(cx - 8 * d, 3 + bob, hull);  // oar
            if (cx + 8 >= 0 && cx - 8 < static_cast<int>(g_ov->width())) {  // ripples only once the hull is in view
                if (_t % 16 < 8) {
                    put(cx - 10, 1 + bob, ripple);
                    put(cx + 10, 1 + bob, ripple);
                } else {
                    put(cx - 11, 0, ripple);
                    put(cx + 11, 0, ripple);
                }
            }
            if (_phase >= 1 && _phase <= 3) {
                const int hy = rndf(_y);
                const float sway = sinf(_t * 0.06F) * 1.5F;
                const uint8_t line = C(210, 220, 230), hook = C(170, 175, 185);
                for (int r = 3 + bob; r < hy; r++) {
                    put(cx + rndf(sway * (r - 3) / static_cast<float>(hy - 3 > 1 ? hy - 3 : 1)), r, line);
                }
                const int hx = cx + rndf(sway);
                fill(hx, hy, 1, 4, hook);
                put(hx - 1, hy + 4, hook);
                put(hx - 2, hy + 3, hook);
                put(hx - 2, hy + 2, hook);  // J-hook
                const int bw = tw + 4;
                board(hx - bw / 2, hy + 3, _text, inkColor());
            }
            break;
        }
        case Kind::Neon: {
            const int x = rndf(_x);
            const int b = base + static_cast<int>(_a);
            const int bw = tw + 6;
            const int bh = FONT_H + 6;
            const int left = x - bw / 2;
            const int top = b - 3 - bh;
            g_clip = ground + 1;
            bool lit = false;
            if (_phase == 2) {
                lit = (_timer % 94) / 2 != 23;  // a two-frame blackout every four seconds
            } else if (_phase == 1 || _phase == 3) {
                const int i = _timer / 2 < 12 ? _timer / 2 : 12;
                lit = FLICKER[i] == (_phase == 1 ? 1 : 0);
            }
            fill(x - 4, b - 3, 1, 3, C(70, 70, 85));
            fill(x + 4, b - 3, 1, 3, C(70, 70, 85));
            fill(left, top, bw, bh, C(12, 12, 24));
            const uint8_t border = lit ? CA(255, 80, 180, 255) : C(80, 35, 65);  // lit tubes glow at night too
            const uint8_t tube = lit ? CA(120, 255, 230, 255) : C(40, 60, 70);
            if (lit) {
                const uint8_t halo = CA(255, 120, 200, 77);
                fill(left - 1, top - 1, bw + 2, 1, halo);
                fill(left - 1, top + bh, bw + 2, 1, halo);
                fill(left - 1, top, 1, bh, halo);
                fill(left + bw, top, 1, bh, halo);
            }
            fill(left, top, bw, 1, border);
            fill(left, top + bh - 1, bw, 1, border);
            fill(left, top, 1, bh, border);
            fill(left + bw - 1, top, 1, bh, border);
            text(_text, left + 3, top + 3, tube);  // no glow on the digits, they stay crisp
            g_clip = 1000;
            break;
        }
        case Kind::Parade: {
            const int mxp = rndf(_x);
            const uint8_t shell = C(220, 60, 60), dark = C(120, 30, 25), eye = C(30, 30, 40), wood = C(110, 80, 50);
            const int stepk = static_cast<int>(_a) % 2;
            const bool pause = _phase == GAIT_PAUSE;
            const bool scuttle = _phase == GAIT_SCUTTLE;
            const int wig = pause ? rndf(sinf(_t * 0.2F)) : 0;
            const int open = _b > 3 ? 1 : 0;
            const int bob = (scuttle && stepk) ? -1 : 0;
            fill(mxp - 3, base - 3 + bob, 7, 3, shell);  // the mother
            fill(mxp - 2, base - 4 + bob, 5, 1, shell);
            fill(mxp - 3, base - 1 + bob, 7, 1, dark);
            put(mxp - 2 + wig, base - 5 + bob, shell);
            put(mxp + 2 + wig, base - 5 + bob, shell);
            put(mxp - 2 + wig, base - 6 + bob, eye);
            put(mxp + 2 + wig, base - 6 + bob, eye);
            for (int i = 0; i < 3; i++) {
                const int ly = pause ? 0 : ((stepk == i % 2) ? 0 : -1);
                put(mxp - 4 - i, base - 1 + ly, dark);
                put(mxp + 4 + i, base - 1 + ly, dark);
            }
            fill(mxp - 5, base - 3 + bob - open, 2, 1, shell);
            fill(mxp - 5, base - 2 + bob, 2, 1, shell);
            fill(mxp + 4, base - 3 + bob - open, 2, 1, shell);
            fill(mxp + 4, base - 2 + bob, 2, 1, shell);
            char one[2] = {'0', '\0'};
            const int len = static_cast<int>(strlen(_text));
            const uint8_t ink = inkColor();
            for (int i = 0; i < len; i++) {  // each child carries one character; walking right, the first one is at the back
                const int cx = mxp - d * (12 + i * 9);
                const int hop = (scuttle && (stepk + i) % 2) ? -1 : 0;  // the children bounce out of phase
                fill(cx - 2, base - 2 + hop, 5, 2, shell);
                put(cx - 1 + wig, base - 3 + hop, eye);
                put(cx + 1 + wig, base - 3 + hop, eye);
                for (int k = 0; k < 2; k++) {
                    const int ly = pause ? 0 : (((stepk + i + k) % 2) ? -1 : 0);
                    put(cx - 3 - k, base - 1 + hop + ly + k, dark);
                    put(cx + 3 + k, base - 1 + hop + ly + k, dark);
                }
                fill(cx, base - 5 + hop, 1, 3, wood);
                one[0] = _text[d > 0 ? len - 1 - i : i];
                board(cx - 3, base - 5 + hop - (FONT_H + 4), one, ink);
            }
            break;
        }
    }
}
