#pragma once

// =============================================================================
//  Paramètres réseau/LMS  →  éditer data/config.json, pas ici !
//  Flash avec : pio run -t uploadfs
// =============================================================================

// =============================================================================
//  Affichage (réglages de comportement, compilés dans le firmware)
// =============================================================================
#define DISPLAY_BRIGHTNESS 200   // 0-255, luminosité du rétroéclairage
#define SCROLL_DELAY_MS    100   // ms entre chaque pixel de défilement
#define SCROLL_PAUSE_MS   2000   // ms de pause en début/fin de défilement
#define POLL_INTERVAL_MS    1000  // ms entre deux interrogations du serveur LMS
#define PLAYER_LOST_POLLS      5  // polls consécutifs sans player → bascule en horloge (~5 s)

#define LONG_PRESS_MS        800  // ms pour déclencher le long-press (menu)
#define TOUCH_DEBOUNCE_MS    400  // ms minimum entre deux taps courts
#define TOUCH_GLITCH_MS      150  // ms de tolérance aux parasites XPT2046

// =============================================================================
//  Broches CYD (ESP32-2432S028) — normalement pas besoin de modifier
// =============================================================================

// Écran ILI9341 sur HSPI
#define TFT_SCLK  14
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1
#define TFT_BL    21   // rétroéclairage (PWM)

// Tactile XPT2046 sur VSPI
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36

// Rotation tactile — ajuster si le toucher est décalé/inversé (0-3)
#define TOUCH_ROTATION 0
