#pragma once
#include <Arduino.h>

// Paramètres lus depuis /config.json sur le filesystem LittleFS.
// Pour modifier : éditer data/config.json puis flasher avec
//   pio run -t uploadfs

struct AppConfig {
    char wifi_ssid    [64];
    char wifi_password[64];
    char lms_ip       [32];
    int  lms_port;
    char lms_player   [64];   // vide = premier player en lecture
    char timezone     [64];   // nom IANA, ex: "Europe/Paris", "Asia/Shanghai"
    char timezone2    [64];   // second fuseau (optionnel, "" = désactivé)
    char clock_style  [16];   // "digital" ou "analog"
    bool valid;               // false si le fichier est absent ou invalide
};

// Charge /config.json depuis LittleFS.
// Retourne true si la lecture a réussi.
// En cas d'échec, cfg.valid == false et les champs sont vides.
bool loadAppConfig(AppConfig& cfg);

// Sauvegarde cfg dans /config.json sur LittleFS.
// Retourne true si l'écriture a réussi.
bool saveAppConfig(const AppConfig& cfg);
