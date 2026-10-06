// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagic Open Firmware
 * Copyright (C) 2026 Times-Z
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <ArduinoJson.h>
#include <LittleFS.h>

#include <Logger.h>
#include "config/ConfigManager.h"
#include "config/SecureStorage.h"

ConfigManager::ConfigManager(const char* filename) : filename(filename), secure() {}

/**
 * @brief Loads the configuration from a file stored in SPIFFS
 *
 * @return true if the configuration was successfully loaded and parsed false otherwise
 */
auto ConfigManager::load() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");
        return false;
    }

    File file = LittleFS.open(filename.c_str(), "r");
    if (!file) {
        Logger::error("Failed to open config file", "ConfigManager");
        return false;
    }

    size_t size = file.size();
    if (size == 0) {
        Logger::warn("Config file is empty", "ConfigManager");
        file.close();
        return false;
    }

    std::unique_ptr<char[]> buf(new char[size + 1]);
    file.readBytes(buf.get(), size);
    buf[size] = '\0';
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf.get());
    if (error) {
        Logger::error(("Failed to parse config file : " + String(error.c_str())).c_str(), "ConfigManager");
        return false;
    }

    String ssid = doc["wifi_ssid"] | "";
    String password = doc["wifi_password"] | "";
    String api_token = doc["api_token"] | "";
    String ntp_server_cfg = doc["ntp_server"] | "";
    this->daytime_mode = doc["daytime_mode"] | daytime_mode;
    this->weather_mode = doc["weather_mode"] | weather_mode;
    this->display_saturation = doc["display_saturation"] | display_saturation;
    this->display_contrast = doc["display_contrast"] | display_contrast;
    this->latitude = doc["latitude"] | latitude;
    this->longitude = doc["longitude"] | longitude;
    String timezone_cfg = doc["timezone"] | "";
    if (timezone_cfg.length() != 0) {
        this->timezone = timezone_cfg.c_str();
    }
    this->scene_morning = doc["scene_morning"] | scene_morning.c_str();
    this->scene_day = doc["scene_day"] | scene_day.c_str();
    this->scene_evening = doc["scene_evening"] | scene_evening.c_str();
    this->scene_night = doc["scene_night"] | scene_night.c_str();
    String animation_file_cfg = doc["animation_file"] | "";
    if (animation_file_cfg.length() != 0) {
        this->animation_file = animation_file_cfg.c_str();
    }

    this->lcd_rotation = doc["lcd_rotation"] | lcd_rotation;

    String nvs_ssid = secure.get("wifi_ssid", "");
    String nvs_password = secure.get("wifi_password", "");
    String nvs_api_token = secure.get("api_token", "");

    if ((ssid.length() != 0 && nvs_ssid.length() == 0) || (password.length() != 0 && nvs_password.length() == 0)) {
        secure.put("wifi_ssid", ssid.c_str());
        secure.put("wifi_password", password.c_str());

        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();

        if (ntp_server_cfg.length() != 0) {
            this->ntp_server = ntp_server_cfg.c_str();
        }

        // Ensure we delete the wifi credentials from the json config after migrating
        ConfigManager::save();

        Logger::info("WiFi credentials migrated to SecureStorage", "ConfigManager");
    } else {
        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();
    }

    if (api_token.length() != 0 && nvs_api_token.length() == 0) {
        secure.put("api_token", api_token.c_str());
        this->api_token = secure.get("api_token").c_str();

        // Ensure we delete the api token from the json config after migrating
        ConfigManager::save();

        Logger::info("API token migrated to SecureStorage", "ConfigManager");
    } else {
        this->api_token = secure.get("api_token").c_str();
    }

    return true;
}

/**
 * @brief Retrieves the current Wi-Fi SSID
 *
 * @return The SSID as a c style string
 */
auto ConfigManager::getSSID() const -> const char* { return ssid.c_str(); }

/**
 * @brief Retrieves the current Wi-Fi password
 *
 * @return The password as a c style string
 */
auto ConfigManager::getPassword() const -> const char* { return password.c_str(); }

/**
 * @brief Retrieves the current API token
 *
 * @return The API token as a c style string
 */
auto ConfigManager::getApiToken() const -> const char* { return api_token.c_str(); }

/**
 * @brief Retrieves the LCD rotation setting
 *
 * @return The rotation of the LCD
 */
auto ConfigManager::getLCDRotation() const -> uint8_t { return lcd_rotation; }

/**
 * @brief Set LCD rotation in memory
 *
 * @param newRotation Rotation value in range [0, 7]
 *
 * @return void
 */
auto ConfigManager::setLCDRotation(uint8_t newRotation) -> void { lcd_rotation = newRotation; }

/**
 * @brief Set WiFi credentials in memory
 * @param newSsid The SSID
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setWiFi(const char* newSsid, const char* newPassword) -> void {
    if (newSsid != nullptr) {
        ssid = newSsid;
    }
    if (newPassword != nullptr) {
        password = newPassword;
    }
}
/**
 * @brief Set WiFi credentials in memory
 * @param newSsid The SSID
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setApiToken(const char* newApiToken) -> void {
    if (newApiToken != nullptr) {
        api_token = newApiToken;
    }
}

/**
 * @brief Save the current configuration to the file
 *
 * @param clearWifiCreds If true wifi credentials will be cleared from json config
 *
 * @return true if the configuration was successfully saved false otherwise
 */
auto ConfigManager::save() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");

        return false;
    }

    File file = LittleFS.open(filename.c_str(), "w");

    if (!file) {
        Logger::error("Failed to open config file for writing", "ConfigManager");

        return false;
    }

    JsonDocument doc;

    secure.put("wifi_ssid", this->getSSID());
    secure.put("wifi_password", this->getPassword());

    doc["lcd_rotation"] = lcd_rotation;
    if (!this->ntp_server.empty()) {
        doc["ntp_server"] = this->ntp_server.c_str();
    }
    doc["animation_file"] = this->animation_file.c_str();
    doc["daytime_mode"] = this->daytime_mode;
    doc["weather_mode"] = this->weather_mode;
    doc["display_saturation"] = this->display_saturation;
    doc["display_contrast"] = this->display_contrast;
    doc["latitude"] = this->latitude;
    doc["longitude"] = this->longitude;
    doc["timezone"] = this->timezone.c_str();
    doc["scene_morning"] = this->scene_morning.c_str();
    doc["scene_day"] = this->scene_day.c_str();
    doc["scene_evening"] = this->scene_evening.c_str();
    doc["scene_night"] = this->scene_night.c_str();

    if (serializeJson(doc, file) == 0) {
        Logger::error("Failed to write config file", "ConfigManager");
        file.close();

        return false;
    }

    file.close();
    Logger::info("Configuration saved", "ConfigManager");

    return true;
}
