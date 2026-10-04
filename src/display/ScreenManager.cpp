// SPDX-License-Identifier: GPL-3.0-or-later
#include "display/ScreenManager.h"

#include <Logger.h>
#include <cstring>
#include "display/DisplayManager.h"

std::array<Screen*, ScreenManager::MAX_SCREENS> ScreenManager::_screens{};
size_t ScreenManager::_count = 0;
Screen* ScreenManager::_active = nullptr;
bool ScreenManager::_needsEnter = false;
bool ScreenManager::_suspendedByGif = false;

auto ScreenManager::registerScreen(Screen* screen) -> void {
    if (screen == nullptr || _count >= MAX_SCREENS) {
        return;
    }
    _screens.at(_count++) = screen;
}

auto ScreenManager::begin(Screen* initial) -> void {
    if (initial != nullptr && find(initial->name()) == nullptr) {
        registerScreen(initial);
    }
    _active = initial;
    _needsEnter = (initial != nullptr);

    if (initial != nullptr) {
        Logger::info((String("Initial screen: ") + initial->name()).c_str(), "ScreenManager");
    }
}

auto ScreenManager::loop() -> void {
    if (_active == nullptr) {
        return;
    }

    // The GIF player paints the whole display itself; resume with a full
    // repaint once it is done.
    if (DisplayManager::isGifPlaying()) {
        _suspendedByGif = true;
        return;
    }

    if (_suspendedByGif) {
        _suspendedByGif = false;
        _needsEnter = true;
    }

    Arduino_GFX* gfx = DisplayManager::getGfx();

    if (_needsEnter) {
        _active->onEnter(gfx);
        _needsEnter = false;
    }

    _active->update(gfx, millis());
}

auto ScreenManager::show(Screen* screen) -> bool {
    if (screen == nullptr) {
        return false;
    }
    if (_active != nullptr && _active != screen) {
        _active->onExit(DisplayManager::getGfx());
    }
    _active = screen;
    _needsEnter = true;
    Logger::info((String("Screen: ") + screen->name()).c_str(), "ScreenManager");
    return true;
}

auto ScreenManager::find(const char* name) -> Screen* {
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < _count; i++) {
        if (strcmp(_screens.at(i)->name(), name) == 0) {
            return _screens.at(i);
        }
    }
    return nullptr;
}

auto ScreenManager::invalidate() -> void { _needsEnter = true; }

auto ScreenManager::active() -> Screen* { return _active; }

auto ScreenManager::screens() -> const std::array<Screen*, MAX_SCREENS>& { return _screens; }

auto ScreenManager::screenCount() -> size_t { return _count; }
