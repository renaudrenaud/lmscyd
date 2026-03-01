#include "app_config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool loadAppConfig(AppConfig& cfg) {
    cfg = AppConfig{};   // tout à zéro / vide
    cfg.lms_port = 9000; // valeur par défaut

    if (!LittleFS.begin(false)) {
        Serial.println("[config] Cannot mount LittleFS");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[config] /config.json not found");
        LittleFS.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    LittleFS.end();

    if (err) {
        Serial.printf("[config] JSON error: %s\n", err.c_str());
        return false;
    }

    // Champs obligatoires
    if (!doc["wifi_ssid"].is<const char*>() ||
        !doc["lms_ip"].is<const char*>()) {
        Serial.println("[config] Missing required fields (wifi_ssid, lms_ip)");
        return false;
    }

    strlcpy(cfg.wifi_ssid,     doc["wifi_ssid"]     | "", sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password, doc["wifi_password"] | "", sizeof(cfg.wifi_password));
    strlcpy(cfg.lms_ip,        doc["lms_ip"]        | "", sizeof(cfg.lms_ip));
    cfg.lms_port = doc["lms_port"] | 9000;
    strlcpy(cfg.lms_player,    doc["lms_player"]    | "", sizeof(cfg.lms_player));
    strlcpy(cfg.timezone,      doc["timezone"]      | "UTC", sizeof(cfg.timezone));

    cfg.valid = true;

    Serial.printf("[config] OK  wifi=%s  lms=%s:%d  player='%s'\n",
                  cfg.wifi_ssid, cfg.lms_ip, cfg.lms_port, cfg.lms_player);
    return true;
}
