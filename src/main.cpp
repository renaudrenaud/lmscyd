//  LMS CYD — Logitech Media Server → ESP32 Cheap Yellow Display (ESP32-2432S028)

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>
#include <ctype.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "version.h"
#include "config.h"
#include "app_config.h"
#include "lms_client.h"

// =============================================================================
//  Configuration LovyanGFX pour CYD (ESP32-2432S028)
// =============================================================================
class LGFX_CYD : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341   _panel;
    lgfx::Bus_SPI         _bus;
    lgfx::Light_PWM       _light;
    lgfx::Touch_XPT2046   _touch;

public:
    LGFX_CYD() {
        // --- Bus SPI de l'écran (HSPI) ---
        {
            auto cfg = _bus.config();
            cfg.spi_host   = HSPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = true;
            cfg.use_lock   = true;
            cfg.dma_channel= SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = TFT_SCLK;
            cfg.pin_mosi   = TFT_MOSI;
            cfg.pin_miso   = TFT_MISO;
            cfg.pin_dc     = TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        // --- Panneau ILI9341 ---
        {
            auto cfg = _panel.config();
            cfg.pin_cs          = TFT_CS;
            cfg.pin_rst         = TFT_RST;
            cfg.pin_busy        = -1;
            cfg.panel_width     = 240;
            cfg.panel_height    = 320;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.invert          = false;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = true;
            _panel.config(cfg);
        }
        // --- Rétroéclairage PWM ---
        {
            auto cfg = _light.config();
            cfg.pin_bl     = TFT_BL;
            cfg.invert     = false;
            cfg.freq       = 44100;
            cfg.pwm_channel= 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        // --- Tactile XPT2046 (VSPI) ---
        {
            auto cfg = _touch.config();
            cfg.x_min          = 300;
            cfg.x_max          = 3900;
            cfg.y_min          = 300;
            cfg.y_max          = 3900;
            cfg.pin_int        = TOUCH_IRQ;
            cfg.bus_shared     = false;
            cfg.offset_rotation= TOUCH_ROTATION;
            cfg.spi_host       = VSPI_HOST;
            cfg.freq           = 1000000;
            cfg.pin_sclk       = TOUCH_CLK;
            cfg.pin_mosi       = TOUCH_MOSI;
            cfg.pin_miso       = TOUCH_MISO;
            cfg.pin_cs         = TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

// =============================================================================
//  Palette de couleurs (RGB565)
// =============================================================================
static const uint32_t C_BG         = 0x000000u;
static const uint32_t C_HEADER_BG  = 0x001030u;
static const uint32_t C_ARTIST     = 0xFFFF00u;   // jaune
static const uint32_t C_ALBUM      = 0x00FFFFu;   // cyan
static const uint32_t C_TITLE      = 0xFFFFFFu;   // blanc
static const uint32_t C_PROGRESS   = 0x00CC00u;   // vert
static const uint32_t C_TRACK_BG   = 0x303030u;   // gris sombre
static const uint32_t C_VOLUME     = 0xFF8000u;   // orange
static const uint32_t C_FORMAT     = 0xA0A0A0u;   // gris clair
static const uint32_t C_PLAY_ICON  = 0x00FF00u;   // vert vif
static const uint32_t C_PAUSE_ICON = 0xFF8000u;   // orange
static const uint32_t C_CLOCK      = 0xFFFFFFu;
static const uint32_t C_SEPARATOR  = 0x203060u;

// =============================================================================
//  État de l'interface
// =============================================================================
enum Screen { SCR_MAIN, SCR_CLOCK, SCR_INFO_SRV, SCR_INFO_PLY, SCR_PORTAL };
static Screen currentScreen = SCR_MAIN;

// =============================================================================
//  Zones de l'écran (paysage 320×240)
// =============================================================================
static const int SCREEN_W   = 320;
static const int SCREEN_H   = 240;
static const int MARGIN     = 6;

// Barre latérale de navigation (gauche, 5 boutons)
static const int SIDEBAR_W  = 50;
static const int SIDEBAR_BTN_H = SCREEN_H / 5;  // 48px par bouton

// En-tête (pleine largeur moins sidebar)
static const int HDR_Y      = 0;
static const int HDR_H      = 28;

// Pochette d'album (colonne gauche, après sidebar)
static const int ART_X      = SIDEBAR_W;      // 50
static const int ART_Y      = HDR_H;          // 28
static const int ART_W      = 120;
static const int ART_H      = 120;            // se termine à y=148

// Infos texte (colonne droite, à côté de la pochette)
static const int INFO_X     = ART_X + ART_W + 4;   // 174
static const int INFO_W     = SCREEN_W - SIDEBAR_W - INFO_X;  // 146
static const int ARTIST_Y   = ART_Y;       // 28,  h=40  → y=68
static const int ARTIST_H   = 40;
static const int ALBUM_Y    = 70;           //       h=34  → y=104
static const int ALBUM_H    = 34;
static const int TITLE_Y    = 106;          //       h=42  → y=148 = ART_Y+ART_H ✓
static const int TITLE_H    = 42;

// Zones basse (pleine largeur, sous la pochette)
static const int PROG_Y     = ART_Y + ART_H + 2;  // 150
static const int PROG_H     = 34;
static const int FMT_Y      = PROG_Y + PROG_H;    // 184
static const int FMT_H      = 22;
static const int CTRL_Y     = FMT_Y + FMT_H;      // 206
static const int CTRL_H     = 34;                  // 206+34=240 ✓

static const int TEXT_W     = SCREEN_W - SIDEBAR_W - 2 * MARGIN;  // pour progress/format

// =============================================================================
//  Gestion du défilement de texte
// =============================================================================
struct TextScroller {
    int           offset      = 0;
    unsigned long pauseStart  = 0;
    unsigned long lastScroll  = 0;
    bool          pausing     = true;
    bool          atEnd       = false;

    void reset() {
        offset     = 0;
        pauseStart = millis();
        lastScroll = 0;
        pausing    = true;
        atEnd      = false;
    }

    int tick(int textW, int viewW) {
        if (textW <= viewW) { offset = 0; return 0; }
        unsigned long now = millis();
        int maxOffset = textW - viewW;

        if (pausing) {
            if (now - pauseStart >= SCROLL_PAUSE_MS) {
                pausing = false;
                if (atEnd) { offset = 0; atEnd = false; }
            }
            return offset;
        }

        if (now - lastScroll >= SCROLL_DELAY_MS) {
            lastScroll = now;
            offset++;
            if (offset >= maxOffset) {
                offset     = maxOffset;
                atEnd      = true;
                pausing    = true;
                pauseStart = now;
            }
        }
        return offset;
    }
};

// =============================================================================
//  Objets globaux
// =============================================================================
static LGFX_CYD  display;
static AppConfig appCfg;
static LmsClient lms;

static ServerStatus serverStatus;
static unsigned long serverStatusAge = 0;  // millis() du dernier succès
static PlayerStatus playerStatus;
static TrackInfo    trackInfo;

static TextScroller scrollArtist;
static TextScroller scrollAlbum;
static TextScroller scrollTitle;

static int   lastArtistOffset = -1;
static int   lastAlbumOffset  = -1;
static int   lastTitleOffset  = -1;
static int   lastVolume       = -1;
static int   lastSongId       = -1;
static float lastElapsed      = -1;
static bool  lastIsPlaying    = false;
static int   lastRssiBars     = -1;
static bool  fullRedrawNeeded = true;

static unsigned long lastPoll    = 0;
static unsigned long lastClock   = 0;

// Ancre temps locale pour inférer elapsed entre deux polls LMS
static float         g_elapsedAnchor   = 0;
static unsigned long g_elapsedAnchorMs = 0;

static bool      clockNeedsFullRedraw = true;
static bool      clockPrevValid       = false;  // true si clockPrevT contient une heure valide
static struct tm clockPrevT           = {};

// Web portal
static WebServer* g_portalServer = nullptr;
static DNSServer  g_dnsServer;

// Cache de la pochette courante
static uint8_t* g_coverBuf    = nullptr;
static size_t   g_coverLen    = 0;
static int      g_coverSongId = -1;   // -1 = jamais chargé (0 = morceau valide, >0 = morceau avec ID)
static char     g_coverErr[80] = "";  // raison d'échec (debug)

// =============================================================================
//  Pochette d'album
// =============================================================================
static void downloadCover(int songId) {
    // Radio (songId == 0) : pas de pochette, on marque comme "traité" sans erreur
    if (songId == 0) {
        if (g_coverBuf) { free(g_coverBuf); g_coverBuf = nullptr; g_coverLen = 0; }
        g_coverSongId = 0;
        g_coverErr[0] = '\0';
        return;
    }

    if (g_coverSongId == songId && g_coverBuf) return;   // déjà en cache

    if (g_coverBuf) { free(g_coverBuf); g_coverBuf = nullptr; g_coverLen = 0; }
    g_coverSongId = songId;
    g_coverErr[0] = '\0';

    if (WiFi.status() != WL_CONNECTED) {
        snprintf(g_coverErr, sizeof(g_coverErr), "No WiFi");
        return;
    }

    String url = String("http://") + appCfg.lms_ip + ":" + appCfg.lms_port
               + "/music/" + songId + "/cover_" + ART_W + "x" + ART_H + "_p";

    WiFiClient wifiClient;
    HTTPClient http;
    http.begin(wifiClient, url);
    http.setTimeout(4000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        snprintf(g_coverErr, sizeof(g_coverErr), "HTTP %d", httpCode);
        http.end();
        return;
    }

    int contentLen = http.getSize();
    const size_t MAX_COVER = 65536;   // 64 KB

    if (contentLen > (int)MAX_COVER) {
        snprintf(g_coverErr, sizeof(g_coverErr), "Too large: %d B", contentLen);
        http.end();
        return;
    }

    // Pour les transferts chunked (contentLen == -1), on part sur 16 KB et on realloc si besoin
    size_t allocLen = (contentLen > 0) ? (size_t)contentLen : 16384;
    uint8_t* buf = (uint8_t*)malloc(allocLen);
    if (!buf) {
        snprintf(g_coverErr, sizeof(g_coverErr), "malloc fail %uB", (unsigned)allocLen);
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t t = millis();
    bool downloadError = false;

    while (millis() - t < 4000 && http.connected()) {
        int avail = stream->available();
        if (avail > 0) {
            // Agrandir le buffer si nécessaire (cas chunked)
            if (total + (size_t)avail > allocLen) {
                size_t newLen = min(allocLen * 2, MAX_COVER);
                if (newLen <= allocLen) {
                    snprintf(g_coverErr, sizeof(g_coverErr), "Cap 64KB cl=%d rd=%u", contentLen, (unsigned)total);
                    downloadError = true;
                    break;
                }
                uint8_t* tmp = (uint8_t*)realloc(buf, newLen);
                if (!tmp) {
                    snprintf(g_coverErr, sizeof(g_coverErr), "realloc fail %uB", (unsigned)newLen);
                    downloadError = true;
                    break;
                }
                buf = tmp;
                allocLen = newLen;
            }
            int rd = stream->readBytes(buf + total,
                         (int)min((size_t)avail, allocLen - total));
            total += rd;
            if (contentLen > 0 && total >= (size_t)contentLen) break;
        } else if (contentLen > 0 && total >= (size_t)contentLen) {
            break;
        }
        delay(1);
    }
    http.end();

    if (!downloadError && total > 0 && g_coverErr[0] == '\0') {
        g_coverBuf = buf;
        g_coverLen = total;
    } else {
        if (g_coverErr[0] == '\0')
            snprintf(g_coverErr, sizeof(g_coverErr), "0 bytes cl=%d", contentLen);
        free(buf);
    }
}

static void drawCoverError(const char* msg) {
    display.fillRect(ART_X, ART_Y, ART_W, ART_H, C_TRACK_BG);
    display.drawRect(ART_X + 4, ART_Y + 4, ART_W - 8, ART_H - 8, C_SEPARATOR);
    display.setFont(&fonts::Font0);   // 6×8 px
    display.setTextColor(TFT_YELLOW, C_TRACK_BG);
    display.setTextDatum(lgfx::top_left);
    // Découpe le message en lignes de ~18 chars (ART_W=120, font=6px)
    const int LINE_W = 18;
    int y = ART_Y + 6;
    const char* p = msg;
    char line[LINE_W + 1];
    while (*p && y < ART_Y + ART_H - 8) {
        int len = 0;
        while (p[len] && len < LINE_W) len++;
        memcpy(line, p, len); line[len] = '\0';
        display.drawString(line, ART_X + 6, y);
        y += 10;
        p += len;
    }
    display.setTextDatum(lgfx::top_left);
}

static void drawCover() {
    if (g_coverBuf && g_coverLen > 0) {
        bool ok = false;
        bool isPng = (g_coverLen >= 4 &&
                      g_coverBuf[0] == 0x89 && g_coverBuf[1] == 0x50 &&
                      g_coverBuf[2] == 0x4E && g_coverBuf[3] == 0x47);
        if (isPng)
            ok = display.drawPng(g_coverBuf, g_coverLen, ART_X, ART_Y, ART_W, ART_H);
        else
            ok = display.drawJpg(g_coverBuf, g_coverLen, ART_X, ART_Y, ART_W, ART_H);
        if (!ok) {
            char msg[80];
            snprintf(msg, sizeof(msg), "%s err %uB hdr:%02X%02X",
                     isPng ? "PNG" : "JPG", (unsigned)g_coverLen,
                     g_coverBuf[0], g_coverBuf[1]);
            drawCoverError(msg);
        }
    } else {
        // Placeholder : erreur download ou pas encore chargé
        const char* msg = (g_coverErr[0] != '\0') ? g_coverErr : "no cover";
        drawCoverError(msg);
    }
    // Séparateur vertical pochette / texte
    display.drawFastVLine(ART_X + ART_W + 1, ART_Y, ART_H, C_SEPARATOR);
}

// =============================================================================
//  Barre latérale de navigation (5 boutons tactiles)
// =============================================================================
static void drawSidebarIcon(int idx, int cx, int cy, uint32_t col) {
    switch (idx) {
        case 0: // Now Playing : triangle ▶
            display.fillTriangle(cx - 7, cy - 8, cx - 7, cy + 8, cx + 8, cy, col);
            break;
        case 1: // Clock : cercle + aiguilles
            display.drawCircle(cx, cy, 9, col);
            display.drawLine(cx, cy, cx + 4, cy - 5, col); // minute
            display.drawLine(cx, cy, cx,     cy - 7, col); // heure
            display.fillCircle(cx, cy, 1, col);
            break;
        case 2: // Server : rectangle + deux barres
            display.drawRect(cx - 9, cy - 8, 18, 16, col);
            display.drawFastHLine(cx - 7, cy - 3, 14, col);
            display.drawFastHLine(cx - 7, cy + 3, 14, col);
            display.fillRect(cx - 7, cy - 7, 4, 3, col);
            display.fillRect(cx - 7, cy - 1, 4, 3, col);
            break;
        case 3: // Player : écran avec pied
            display.drawRoundRect(cx - 9, cy - 8, 18, 12, 2, col);
            display.drawFastVLine(cx, cy + 4, 4, col);
            display.drawFastHLine(cx - 4, cy + 8, 9, col);
            break;
        case 4: // Config : engrenage simplifié (cercle + 4 dents)
            display.drawCircle(cx, cy, 5, col);
            display.fillCircle(cx, cy, 3, col);
            for (int a = 0; a < 4; a++) {
                float angle = a * 3.14159f / 2.0f;
                int x1 = cx + (int)(6 * cosf(angle));
                int y1 = cy + (int)(6 * sinf(angle));
                int x2 = cx + (int)(9 * cosf(angle));
                int y2 = cy + (int)(9 * sinf(angle));
                display.drawLine(x1, y1, x2, y2, col);
            }
            break;
    }
}

static void drawSidebar() {
    display.fillRect(0, 0, SIDEBAR_W, SCREEN_H, 0x0a0a1a);

    for (int i = 0; i < 5; i++) {
        int y = i * SIDEBAR_BTN_H;
        bool active = (i == 0 && currentScreen == SCR_MAIN)     ||
                      (i == 1 && currentScreen == SCR_CLOCK)    ||
                      (i == 2 && currentScreen == SCR_INFO_SRV) ||
                      (i == 3 && currentScreen == SCR_INFO_PLY) ||
                      (i == 4 && currentScreen == SCR_PORTAL);

        uint32_t bg  = active ? 0x1a2a4aU : 0x0a0a1aU;
        uint32_t col = active ? 0x4a8affU : 0x6070a0U;
        display.fillRect(0, y, SIDEBAR_W, SIDEBAR_BTN_H, bg);
        if (active)
            display.drawFastHLine(0, y, SIDEBAR_W, 0x4a6affU);
        display.drawFastHLine(0, y + SIDEBAR_BTN_H - 1, SIDEBAR_W, 0x152030U);

        drawSidebarIcon(i, SIDEBAR_W / 2, y + SIDEBAR_BTN_H / 2, col);
    }

    display.drawFastVLine(SIDEBAR_W, 0, SCREEN_H, 0x203050U);
}

// =============================================================================
//  Conversion fuseau horaire IANA → POSIX (pour configTzTime)
// =============================================================================
static const char* ianaToposix(const char* iana) {
    static const struct { const char* iana; const char* posix; } TZ[] = {
        { "UTC",                "UTC0"                                      },
        { "Europe/London",      "GMT0BST,M3.5.0/1,M10.5.0"                 },
        { "Europe/Paris",       "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Berlin",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Rome",        "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Madrid",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Amsterdam",   "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Brussels",    "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Zurich",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Helsinki",    "EET-2EEST,M3.5.0/3,M10.5.0/4"            },
        { "Europe/Athens",      "EET-2EEST,M3.5.0/3,M10.5.0/4"            },
        { "Europe/Moscow",      "MSK-3"                                     },
        { "Africa/Cairo",       "EET-2"                                     },
        { "Africa/Johannesburg","SAST-2"                                    },
        { "America/New_York",   "EST5EDT,M3.2.0,M11.1.0"                   },
        { "America/Chicago",    "CST6CDT,M3.2.0,M11.1.0"                   },
        { "America/Denver",     "MST7MDT,M3.2.0,M11.1.0"                   },
        { "America/Phoenix",    "MST7"                                      },
        { "America/Los_Angeles","PST8PDT,M3.2.0,M11.1.0"                   },
        { "America/Anchorage",  "AKST9AKDT,M3.2.0,M11.1.0"                 },
        { "America/Sao_Paulo",  "BRT3BRST,M10.3.0/0,M2.3.0/0"             },
        { "America/Buenos_Aires","ART3"                                     },
        { "America/Mexico_City","CST6CDT,M4.1.0,M10.5.0"                   },
        { "America/Toronto",    "EST5EDT,M3.2.0,M11.1.0"                   },
        { "America/Vancouver",  "PST8PDT,M3.2.0,M11.1.0"                   },
        { "Asia/Dubai",         "GST-4"                                     },
        { "Asia/Karachi",       "PKT-5"                                     },
        { "Asia/Kolkata",       "IST-5:30"                                  },
        { "Asia/Dhaka",         "BST-6"                                     },
        { "Asia/Bangkok",       "ICT-7"                                     },
        { "Asia/Jakarta",       "WIB-7"                                     },
        { "Asia/Shanghai",      "CST-8"                                     },
        { "Asia/Hong_Kong",     "HKT-8"                                     },
        { "Asia/Singapore",     "SGT-8"                                     },
        { "Asia/Taipei",        "CST-8"                                     },
        { "Asia/Tokyo",         "JST-9"                                     },
        { "Asia/Seoul",         "KST-9"                                     },
        { "Australia/Perth",    "AWST-8"                                    },
        { "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"          },
        { "Australia/Sydney",   "AEST-10AEDT,M10.1.0,M4.1.0/3"            },
        { "Pacific/Auckland",   "NZST-12NZDT,M9.5.0,M4.1.0/3"             },
        { nullptr, nullptr }
    };
    for (int i = 0; TZ[i].iana; i++) {
        if (strcmp(iana, TZ[i].iana) == 0) return TZ[i].posix;
    }
    return "UTC0";
}

// =============================================================================
//  Utilitaires
// =============================================================================
static String formatTime(float sec) {
    if (sec < 0) sec = 0;
    int m = (int)sec / 60;
    int s = (int)sec % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return String(buf);
}

static String formatSampleRate(int hz) {
    if (hz == 0) return "";
    if (hz % 1000 == 0) return String(hz / 1000) + "k";
    return String(hz / 1000.0f, 1) + "k";
}

// Dessine un texte dans une zone avec défilement (clip + offset)
static void drawScrollText(int zoneX, int zoneY, int zoneW, int zoneH,
                           const String& text, uint32_t fg,
                           const lgfx::IFont* font, int offset) {
    display.fillRect(zoneX, zoneY, zoneW, zoneH, C_BG);
    display.setFont(font);
    display.setTextColor(fg, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.setClipRect(zoneX, zoneY, zoneW, zoneH);
    display.drawString(text.c_str(), zoneX - offset, zoneY + 10);
    display.clearClipRect();
}

// Dessine une barre de progression (fond + remplissage)
static void drawBar(int x, int y, int w, int h,
                    float ratio, uint32_t fg, uint32_t bg) {
    int filled = (int)(w * constrain(ratio, 0.0f, 1.0f));
    display.fillRect(x, y, filled, h, fg);
    if (filled < w) display.fillRect(x + filled, y, w - filled, h, bg);
}

// Convertit un RSSI (dBm) en niveau 0-4
static int rssiToBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

// Icône WiFi : 4 barres verticales bottom-alignées, actives ou grises
static void drawWifiIcon(int x, int y, int bars) {
    // bars = 0-4, chaque barre = 3px large, 1px d'écart, hauteur croissante
    static const int heights[] = { 4, 7, 10, 13 };
    static const uint32_t colors[] = {
        0xFF2020u,   // 0 bar  → rouge  (dessiné sur la 1ère barre seulement)
        0xFF5500u,   // 1 bar  → orange
        0xFFAA00u,   // 2 bars → jaune
        0x88CC00u,   // 3 bars → vert-jaune
        0x00CC00u,   // 4 bars → vert
    };
    const uint32_t activeColor = colors[bars];
    const uint32_t dimColor    = 0x303030u;
    const int bottom = y + 13;   // base commune des barres

    for (int i = 0; i < 4; i++) {
        int bx = x + i * 4;
        int bh = heights[i];
        uint32_t c = (i < bars) ? activeColor : dimColor;
        display.fillRect(bx, bottom - bh, 3, bh, c);
    }
}

// Icône play (triangle) ou pause (deux barres)
static void drawStatusIcon(int x, int y, bool playing) {
    display.fillRect(x, y, 18, 18, C_HEADER_BG);
    if (playing) {
        display.fillTriangle(x + 2, y + 1, x + 2, y + 16, x + 16, y + 8, C_PLAY_ICON);
    } else {
        display.fillRect(x + 2,  y + 2, 5, 14, C_PAUSE_ICON);
        display.fillRect(x + 11, y + 2, 5, 14, C_PAUSE_ICON);
    }
}

// =============================================================================
//  Dessin de l'en-tête (nom du player + icône + volume)
// =============================================================================
static void drawHeader(bool forceRedraw) {
    int rssiBars = (WiFi.status() == WL_CONNECTED) ? rssiToBars(WiFi.RSSI()) : 0;
    if (!forceRedraw && playerStatus.volume == lastVolume &&
        playerStatus.isPlaying == lastIsPlaying && rssiBars == lastRssiBars) return;

    display.fillRect(SIDEBAR_W, HDR_Y, SCREEN_W - SIDEBAR_W, HDR_H, C_HEADER_BG);

    // Icône play/pause
    drawStatusIcon(SIDEBAR_W + MARGIN, 5, playerStatus.isPlaying);

    // Nom du player (tronqué)
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_left);
    String name = playerStatus.playerName;
    if (name.length() > 10) name = name.substring(0, 10);
    display.drawString(name.c_str(), SIDEBAR_W + 30, 5);

    // Barre de volume (droite, avant l'icône WiFi)
    int volX = 222, volY = 7, volW = 56, volH = 10;
    drawBar(volX, volY, volW, volH, playerStatus.volume / 100.0f, C_VOLUME, C_TRACK_BG);
    display.setFont(&fonts::Font0);
    display.setTextColor(C_FORMAT, C_HEADER_BG);
    char vbuf[5];
    snprintf(vbuf, sizeof(vbuf), "%3d%%", playerStatus.volume);
    display.drawString(vbuf, 226, 18);

    // Icône WiFi (4 barres, extrémité droite du header)
    drawWifiIcon(301, 7, rssiBars);
    lastRssiBars = rssiBars;

    // Sleep timer
    if (playerStatus.hasSleepTimer) {
        char sbuf[16];
        int sm = (int)playerStatus.willSleepIn / 60;
        int ss = (int)playerStatus.willSleepIn % 60;
        snprintf(sbuf, sizeof(sbuf), "zz %02d:%02d", sm, ss);
        display.setFont(&fonts::Font0);
        display.setTextColor(0x8888FFu, C_HEADER_BG);
        display.drawString(sbuf, 170, 18);
    }

    // Ligne séparatrice
    display.drawFastHLine(SIDEBAR_W, HDR_H, SCREEN_W - SIDEBAR_W, C_SEPARATOR);
    display.drawFastHLine(SIDEBAR_W, HDR_H + 1, SCREEN_W - SIDEBAR_W, C_SEPARATOR >> 1);

    lastVolume    = playerStatus.volume;
    lastIsPlaying = playerStatus.isPlaying;
}

// =============================================================================
//  Elapsed inféré localement entre deux polls LMS
// =============================================================================
static float inferredElapsed() {
    if (!playerStatus.isPlaying || g_elapsedAnchorMs == 0)
        return playerStatus.elapsed;
    float v = g_elapsedAnchor + (millis() - g_elapsedAnchorMs) / 1000.0f;
    if (trackInfo.valid && trackInfo.duration > 0 && v > trackInfo.duration)
        v = trackInfo.duration;
    return v;
}

// =============================================================================
//  Dessin de la barre de progression + infos de piste
// =============================================================================
static void drawProgress(bool forceRedraw) {
    float elapsed = inferredElapsed();
    if (!forceRedraw && fabsf(elapsed - lastElapsed) < 0.5f) return;
    lastElapsed = elapsed;

    display.drawFastHLine(SIDEBAR_W, PROG_Y, SCREEN_W - SIDEBAR_W, C_SEPARATOR);

    // Temps écoulé / total
    String elapsedStr = formatTime(elapsed);
    String duration   = trackInfo.valid ? formatTime(trackInfo.duration) : "--:--";
    String pos        = elapsedStr + " / " + duration;

    display.setFont(&fonts::Font2);
    display.setTextColor(C_FORMAT, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.fillRect(SIDEBAR_W, PROG_Y + 2, SCREEN_W - SIDEBAR_W, 14, C_BG);
    display.drawString(pos.c_str(), SIDEBAR_W + MARGIN, PROG_Y + 3);

    // Numéro de piste
    char trkbuf[12];
    snprintf(trkbuf, sizeof(trkbuf), "%d/%d",
             playerStatus.playlistIndex + 1, playerStatus.playlistTracks);
    display.setTextDatum(lgfx::top_right);
    display.drawString(trkbuf, SCREEN_W - MARGIN, PROG_Y + 3);
    display.setTextDatum(lgfx::top_left);

    // Barre de progression
    float ratio = 0;
    if (trackInfo.valid && trackInfo.duration > 0)
        ratio = elapsed / trackInfo.duration;
    drawBar(SIDEBAR_W + MARGIN, PROG_Y + 20, SCREEN_W - SIDEBAR_W - 2 * MARGIN, 10,
            ratio, C_PROGRESS, C_TRACK_BG);
}

// =============================================================================
//  Dessin des infos de format audio
// =============================================================================
static void drawFormatInfo() {
    display.fillRect(SIDEBAR_W, FMT_Y, SCREEN_W - SIDEBAR_W, FMT_H, C_BG);
    display.drawFastHLine(SIDEBAR_W, FMT_Y, SCREEN_W - SIDEBAR_W, C_SEPARATOR);

    if (!trackInfo.valid) return;

    String fmt = trackInfo.fileType;
    fmt.toUpperCase();

    String info;
    if (trackInfo.sampleRate > 0)
        info += formatSampleRate(trackInfo.sampleRate) + "Hz ";
    if (trackInfo.sampleSize > 0)
        info += String(trackInfo.sampleSize) + "bit ";
    if (trackInfo.bitrate.length() > 0)
        info += trackInfo.bitrate;

    display.setFont(&fonts::Font2);
    display.setTextColor(C_FORMAT, C_BG);
    display.setTextDatum(lgfx::top_left);

    String left = fmt.length() > 0 ? fmt + "  " + info : info;
    display.drawString(left.c_str(), SIDEBAR_W + MARGIN, FMT_Y + 4);
}

// =============================================================================
//  Zone de contrôle (indications tactiles discrètes)
// =============================================================================
static void drawControls() {
    display.fillRect(SIDEBAR_W, CTRL_Y, SCREEN_W - SIDEBAR_W, CTRL_H, C_BG);
    display.drawFastHLine(SIDEBAR_W, CTRL_Y, SCREEN_W - SIDEBAR_W, C_SEPARATOR);

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.drawString("|<< PREV", SIDEBAR_W + MARGIN, CTRL_Y + 10);
    display.setTextDatum(lgfx::top_center);
    display.drawString("PLAY/PAUSE", SIDEBAR_W + SCREEN_W / 2, CTRL_Y + 10);
    display.setTextDatum(lgfx::top_right);
    display.drawString("NEXT >>|", SCREEN_W - MARGIN, CTRL_Y + 10);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran de lecture en cours
// =============================================================================
static void drawPlayingScreen(bool full) {
    if (full) {
        display.fillScreen(C_BG);
        drawSidebar();         // barre latérale de navigation
        drawCover();           // pochette depuis le cache (rapide)
        drawControls();
        drawFormatInfo();
    }
    drawHeader(full);
    drawProgress(full);

    // --- Artiste (colonne droite) ---
    String artist = trackInfo.valid ? trackInfo.artist : playerStatus.currentTitle;
    if (artist.isEmpty()) artist = playerStatus.playerName;

    display.setFont(&fonts::FreeSans12pt7b);
    int artistW = display.textWidth(artist.c_str());
    int ao = scrollArtist.tick(artistW, INFO_W);
    if (full || ao != lastArtistOffset) {
        drawScrollText(INFO_X, ARTIST_Y, INFO_W, ARTIST_H, artist, C_ARTIST,
                       &fonts::FreeSans12pt7b, ao);
        lastArtistOffset = ao;
    }

    // --- Album (colonne droite) ---
    String album = trackInfo.valid ? trackInfo.album : "";
    display.setFont(&fonts::FreeSans9pt7b);
    int albumW = display.textWidth(album.c_str());
    int lo = scrollAlbum.tick(albumW, INFO_W);
    if (full || lo != lastAlbumOffset) {
        drawScrollText(INFO_X, ALBUM_Y, INFO_W, ALBUM_H, album, C_ALBUM,
                       &fonts::FreeSans9pt7b, lo);
        lastAlbumOffset = lo;
    }

    // --- Titre (colonne droite) ---
    String title;
    if (trackInfo.valid) {
        title = trackInfo.remoteTitle.length() > 0
                ? trackInfo.remoteTitle + " – " + trackInfo.title
                : trackInfo.title;
    } else {
        title = playerStatus.currentTitle;
    }

    display.setFont(&fonts::FreeSans9pt7b);
    int titleW = display.textWidth(title.c_str());
    int to = scrollTitle.tick(titleW, INFO_W);
    if (full || to != lastTitleOffset) {
        drawScrollText(INFO_X, TITLE_Y, INFO_W, TITLE_H, title, C_TITLE,
                       &fonts::FreeSans9pt7b, to);
        lastTitleOffset = to;
    }
}

// =============================================================================
//  Helpers double fuseau horaire
// =============================================================================

// Renvoie la partie "ville" d'un nom IANA, ex: "Europe/Paris" → "Paris"
static String tzCityName(const char* iana) {
    const char* slash = strrchr(iana, '/');
    return String(slash ? slash + 1 : iana);
}

// Décalage horaire en secondes pour un fuseau POSIX donné.
// Parse une chaîne comme "CET-1CEST..." ou "UTC0" ou "PST8PDT..."
// Retourne le décalage standard (ignore DST pour simplifier).
static int32_t posixTzToOffset(const char* posix) {
    // Formats supportés :
    // "UTC0", "GMT0" → offset = 0
    // "CET-1..." → offset = +1h = 3600s (le signe est inversé dans la norme POSIX)
    // "PST8..." → offset = -8h = -28800s
    // "EET-2..." → offset = +2h = 7200s
    // "IST-5:30" → offset = +5h30 = 19800s
    const char* p = posix;
    // Skip le nom du fuseau (lettres)
    while (*p && (isalpha(*p) || *p == '-')) p++;
    if (!*p) return 0;

    // Lire le décalage heures:minutes optionnelles
    int sign = (*p == '-') ? -1 : 1;  // signe POSIX inversé
    if (*p == '-' || *p == '+') p++;

    int hours = atoi(p);
    while (*p && isdigit(*p)) p++;

    int minutes = 0;
    if (*p == ':') {
        p++;
        minutes = atoi(p);
    }

    int offsetSec = sign * (hours * 3600 + minutes * 60);
    // Inverser le signe pour obtenir le décalage réel (UTC+1 = +3600)
    return -offsetSec;
}

// Retourne l'heure locale dans un fuseau IANA quelconque, DST inclus.
// Utilise setenv/tzset/localtime_r puis restaure le TZ principal.
static bool getTimeInZone(const char* iana, struct tm& t) {
    time_t now;
    time(&now);
    if (now < 1000000) return false;   // NTP pas encore synchronisé

    const char* posix = ianaToposix(iana);

    // Sauvegarder le TZ courant
    char savedTz[64] = "";
    const char* cur = getenv("TZ");
    if (cur) strlcpy(savedTz, cur, sizeof(savedTz));

    setenv("TZ", posix, 1);
    tzset();
    localtime_r(&now, &t);

    // Restaurer le TZ principal
    if (savedTz[0]) setenv("TZ", savedTz, 1);
    else            unsetenv("TZ");
    tzset();

    return true;
}

// =============================================================================
//  Horloge analogique
// =============================================================================
// Partie statique du cadran (cercles + graduations) — redessinée après effacement des aiguilles
static void drawAnalogClockFace() {
    const int   cx = SCREEN_W / 2;
    const int   cy = 96;
    const int   r  = 87;
    const float CLOCK_2PI = 2.0f * M_PI;

    display.drawCircle(cx, cy, r,     C_SEPARATOR);
    display.drawCircle(cx, cy, r - 1, 0x182848u);

    for (int i = 0; i < 60; i++) {
        float a   = i * CLOCK_2PI / 60.0f;
        bool  maj = (i % 5 == 0);
        int   len = maj ? 11 : 4;
        int   r0  = r - 3;
        int   x1  = cx + (int)(r0         * sinf(a) + 0.5f);
        int   y1  = cy - (int)(r0         * cosf(a) + 0.5f);
        int   x2  = cx + (int)((r0 - len) * sinf(a) + 0.5f);
        int   y2  = cy - (int)((r0 - len) * cosf(a) + 0.5f);
        display.drawLine(x1, y1, x2, y2, maj ? C_FORMAT : 0x182848u);
    }
}

// Aiguilles seules — appelée avec les couleurs réelles pour dessiner,
// ou avec C_BG pour effacer sans fillScreen
static void drawAnalogClockHands(const struct tm& t, uint32_t handColor, uint32_t secColor) {
    const int   cx = SCREEN_W / 2;
    const int   cy = 96;
    const float CLOCK_2PI = 2.0f * M_PI;

    auto drawHand = [&](float angle, int length, uint32_t color, int width) {
        float px = cosf(angle);
        float py = sinf(angle);
        int   hx = cx + (int)(length * sinf(angle) + 0.5f);
        int   hy = cy - (int)(length * cosf(angle) + 0.5f);
        for (int i = -(width / 2); i <= width / 2; i++) {
            display.drawLine(
                cx + (int)(i * px + 0.5f), cy + (int)(i * py + 0.5f),
                hx + (int)(i * px + 0.5f), hy + (int)(i * py + 0.5f),
                color);
        }
    };

    drawHand((t.tm_hour % 12 + t.tm_min / 60.0f) * CLOCK_2PI / 12.0f, 48, handColor, 5);
    drawHand((t.tm_min + t.tm_sec / 60.0f) * CLOCK_2PI / 60.0f, 70, handColor, 3);

    float sa  = t.tm_sec * CLOCK_2PI / 60.0f;
    int   sx  = cx + (int)(80 * sinf(sa) + 0.5f);
    int   sy  = cy - (int)(80 * cosf(sa) + 0.5f);
    int   scx = cx - (int)(18 * sinf(sa) + 0.5f);
    int   scy = cy + (int)(18 * cosf(sa) + 0.5f);
    display.drawLine(scx, scy, sx, sy, secColor);

    display.fillCircle(cx, cy, 5, secColor);
    display.fillCircle(cx, cy, 3, handColor);
}

// =============================================================================
//  Écran horloge / veille (aucun player en lecture)
// =============================================================================
static void drawIdleScreen() {
    struct tm t;
    bool hasTime  = getLocalTime(&t, 10);  // timeout 10ms pour ne pas bloquer
    bool isAnalog = (strcmp(appCfg.clock_style, "analog") == 0);
    bool prevValid = !clockNeedsFullRedraw;
    bool hasTz2   = (appCfg.timezone2[0] != '\0');

    // Premier dessin (ou après changement de style / retour depuis lecture) :
    // on efface l'écran une seule fois
    if (clockNeedsFullRedraw) {
        display.fillScreen(C_BG);
        clockNeedsFullRedraw = false;
    }

    if (isAnalog) {
        // ── Horloge analogique — mise à jour des aiguilles sans fillScreen ──
        if (hasTime) {
            if (clockPrevValid)
                drawAnalogClockHands(clockPrevT, C_BG, C_BG); // efface anciennes aiguilles
            drawAnalogClockFace();                              // restaure le cadran
            drawAnalogClockHands(t, C_TITLE, C_VOLUME);        // dessine les nouvelles
            clockPrevT = t;
            clockPrevValid = true;

            char dateBuf[12];
            strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &t);
            display.setFont(&fonts::Font2);
            display.setTextColor(C_FORMAT, C_BG);
            display.setTextDatum(lgfx::top_center);
            display.drawString(dateBuf, SCREEN_W / 2, hasTz2 ? 186 : 196);

            if (hasTz2) {
                struct tm t2;
                if (getTimeInZone(appCfg.timezone2, t2)) {
                    char tz2Buf[32];
                    strftime(tz2Buf, sizeof(tz2Buf), "%H:%M:%S", &t2);
                    char tz2Line[48];
                    snprintf(tz2Line, sizeof(tz2Line), "%s  %s",
                             tzCityName(appCfg.timezone2).c_str(), tz2Buf);
                    display.setTextColor(C_ALBUM, C_BG);
                    display.drawString(tz2Line, SCREEN_W / 2, 204);
                }
            }
            display.setTextDatum(lgfx::top_left);
        }
        display.setFont(&fonts::Font0);
        display.setTextDatum(lgfx::top_center);
        if (serverStatus.valid) {
            char buf[64];
            display.setTextColor(C_FORMAT, C_BG);
            snprintf(buf, sizeof(buf), "LMS v%s  —  %d players  %d albums  %d songs",
                     serverStatus.version.c_str(), serverStatus.playerCount,
                     serverStatus.totalAlbums, serverStatus.totalSongs);
            display.drawString(buf, SCREEN_W / 2, 220);
        } else {
            display.setTextColor(0xFF4040u, C_BG);
            display.drawString((String("LMS unreachable — ") + appCfg.lms_ip).c_str(),
                               SCREEN_W / 2, 220);
        }
        display.setTextDatum(lgfx::top_left);

    } else {
        // ── Horloge digitale — le texte avec fond C_BG écrase lui-même ──────
        if (hasTime) {
            char timeBuf[9], dateBuf[12];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
            strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &t);

            display.setFont(&fonts::FreeSans24pt7b);
            display.setTextColor(C_CLOCK, C_BG);
            display.setTextDatum(lgfx::top_center);
            display.drawString(timeBuf, SCREEN_W / 2, hasTz2 ? 2 : 30);

            if (hasTz2) {
                display.setFont(&fonts::Font2);
            } else {
                display.setFont(&fonts::FreeSans9pt7b);
            }
            display.setTextColor(C_FORMAT, C_BG);
            display.drawString(dateBuf, SCREEN_W / 2, hasTz2 ? 42 : 100);

            if (hasTz2) {
                display.drawFastHLine(MARGIN, 62, SCREEN_W - 2 * MARGIN, C_SEPARATOR);
                display.setFont(&fonts::Font2);
                display.setTextColor(C_FORMAT, C_BG);
                display.drawString(tzCityName(appCfg.timezone2).c_str(), SCREEN_W / 2, 66);

                struct tm t2;
                if (getTimeInZone(appCfg.timezone2, t2)) {
                    char tz2Time[9], tz2Date[12];
                    strftime(tz2Time, sizeof(tz2Time), "%H:%M:%S", &t2);
                    strftime(tz2Date, sizeof(tz2Date), "%d/%m/%Y", &t2);
                    display.setFont(&fonts::FreeSans18pt7b);
                    display.setTextColor(C_CLOCK, C_BG);
                    display.drawString(tz2Time, SCREEN_W / 2, 84);
                    display.setFont(&fonts::Font2);
                    display.setTextColor(C_FORMAT, C_BG);
                    display.drawString(tz2Date, SCREEN_W / 2, 115);
                }
            }
            display.setTextDatum(lgfx::top_left);
        }

        display.setFont(&fonts::Font2);
        display.setTextDatum(lgfx::top_left);
        if (serverStatus.valid) {
            char buf[48];
            display.setTextColor(C_FORMAT, C_BG);
            snprintf(buf, sizeof(buf), "LMS v%s", serverStatus.version.c_str());
            display.drawString(buf, MARGIN, hasTz2 ? 140 : 135);
            snprintf(buf, sizeof(buf), "Players: %d  Albums: %d  Songs: %d",
                     serverStatus.playerCount, serverStatus.totalAlbums, serverStatus.totalSongs);
            display.drawString(buf, MARGIN, hasTz2 ? 156 : 155);
        } else {
            display.setTextColor(0xFF4040u, C_BG);
            display.drawString((String("LMS unreachable — ") + appCfg.lms_ip).c_str(), MARGIN, hasTz2 ? 140 : 135);
        }

        display.setTextColor(0x303030u, C_BG);
        display.setFont(&fonts::Font0);
        display.drawString("Touch left=prev  center=play/pause  right=next", MARGIN, 225);
    }
}

// =============================================================================
//  Écran de connexion WiFi
// =============================================================================
static void drawConnecting(const char* msg) {
    display.fillScreen(C_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString(APP_NAME "  v" APP_VERSION, SCREEN_W / 2, 38);
    display.setFont(&fonts::Font2);
    display.setTextColor(0x404060u, C_BG);
    display.drawString(APP_URL, SCREEN_W / 2, 62);
    display.setTextColor(C_FORMAT, C_BG);
    display.drawString(msg, SCREEN_W / 2, 100);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran de démarrage (infos serveur LMS)
// =============================================================================
static void drawStartup() {
    display.fillScreen(C_BG);
    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(C_ARTIST, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString(APP_NAME "  v" APP_VERSION, SCREEN_W / 2, 18);
    display.setFont(&fonts::Font2);
    display.setTextColor(0x404060u, C_BG);
    display.drawString(APP_URL, SCREEN_W / 2, 46);

    display.setFont(&fonts::Font2);
    display.setTextDatum(lgfx::top_left);
    if (serverStatus.valid) {
        char buf[40];
        display.setTextColor(C_FORMAT, C_BG);
        snprintf(buf, sizeof(buf), "LMS v%s", serverStatus.version.c_str());
        display.drawString(buf, MARGIN, 80);
        snprintf(buf, sizeof(buf), "Players : %d", serverStatus.playerCount);
        display.drawString(buf, MARGIN, 100);
        snprintf(buf, sizeof(buf), "Albums  : %d", serverStatus.totalAlbums);
        display.drawString(buf, MARGIN, 120);
        snprintf(buf, sizeof(buf), "Songs   : %d", serverStatus.totalSongs);
        display.drawString(buf, MARGIN, 140);
    } else {
        display.setTextColor(0xFF4040u, C_BG);
        display.drawString("LMS server not found!", MARGIN, 80);
        display.setTextColor(C_FORMAT, C_BG);
        String lmsAddr = String(appCfg.lms_ip) + ":" + appCfg.lms_port;
        display.drawString(lmsAddr.c_str(), MARGIN, 100);
    }
    display.setTextDatum(lgfx::top_left);
    delay(2000);
}

// =============================================================================
//  Web portal — configuration via navigateur
// =============================================================================

static const char PORTAL_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LMS CYD</title><style>
body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
h1{color:#333;font-size:1.4em}
label{display:block;margin-top:1em;font-weight:bold;font-size:.9em}
input{width:100%;padding:.4em;box-sizing:border-box;border:1px solid #ccc;border-radius:3px}
.hint{font-weight:normal;color:#888;font-size:.8em}
button{display:block;width:100%;margin-top:1.5em;padding:.7em;background:#0055cc;
       color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer}
button:hover{background:#003a9e}</style></head><body>
<h1>LMS CYD — Configuration</h1>
<form method="POST" action="/save">
<label>WiFi SSID</label>
<input name="ssid" value="%SSID%" required>
<label>WiFi Password <span class="hint">(leave empty to keep current)</span></label>
<input name="pass" type="password" placeholder="(unchanged)">
<label>LMS IP address</label>
<input name="ip" value="%IP%" required>
<label>LMS Port</label>
<input name="port" value="%PORT%">
<label>Player name <span class="hint">(empty = first active)</span></label>
<input name="player" value="%PLAYER%">
<label>Timezone <span class="hint">e.g. Europe/Paris, Asia/Shanghai</span></label>
<input name="tz" value="%TZ%">
<label>Timezone 2 <span class="hint">(optional — leave empty to disable)</span></label>
<input name="tz2" value="%TZ2%">
<label>Clock style</label>
<select name="clock_style" style="width:100%;padding:.4em;border:1px solid #ccc;border-radius:3px">
<option value="digital"%SEL_DIG%>Digital</option>
<option value="analog"%SEL_ANA%>Analog</option>
<option value="casio"%SEL_CASIO%>G-Shock</option>
</select>
<button type="submit">Save &amp; Reboot</button>
</form></body></html>)rawhtml";

static const char PORTAL_SAVED[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<style>body{font-family:sans-serif;text-align:center;padding:3em}"
    "h2{color:green}</style></head><body>"
    "<h2>Saved!</h2><p>The device is rebooting&hellip;</p></body></html>";

static void drawPortalScreen() {
    display.fillScreen(C_BG);
    display.fillRect(0, 0, SCREEN_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Web Portal", SCREEN_W / 2, 6);
    display.drawFastHLine(0, HDR_H, SCREEN_W, C_SEPARATOR);

    display.setTextColor(C_FORMAT, C_BG);
    display.drawString("Connect your phone to:", SCREEN_W / 2, 46);
    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(C_ARTIST, C_BG);
    display.drawString("LMS-CYD-Config", SCREEN_W / 2, 68);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_FORMAT, C_BG);
    display.drawString("Then open browser at:", SCREEN_W / 2, 110);
    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(C_ALBUM, C_BG);
    display.drawString("192.168.4.1", SCREEN_W / 2, 132);

    display.setFont(&fonts::Font2);
    display.setTextColor(0x404040u, C_BG);
    display.drawString("Hold screen to cancel", SCREEN_W / 2, 200);
    display.setTextDatum(lgfx::top_left);
}

static void portalHandleRoot() {
    String html = FPSTR(PORTAL_HTML);
    html.replace("%SSID%",   String(appCfg.wifi_ssid));
    html.replace("%IP%",     String(appCfg.lms_ip));
    html.replace("%PORT%",   String(appCfg.lms_port));
    html.replace("%PLAYER%", String(appCfg.lms_player));
    html.replace("%TZ%",     String(appCfg.timezone));
    html.replace("%TZ2%",    String(appCfg.timezone2));
    bool isAnalog = (strcmp(appCfg.clock_style, "analog") == 0);
    bool isCasio  = (strcmp(appCfg.clock_style, "casio")  == 0);
    html.replace("%SEL_DIG%",   (!isAnalog && !isCasio) ? " selected" : "");
    html.replace("%SEL_ANA%",   isAnalog                ? " selected" : "");
    html.replace("%SEL_CASIO%", isCasio                 ? " selected" : "");
    g_portalServer->send(200, "text/html; charset=utf-8", html);
}

static void portalHandleSave() {
    String ssid   = g_portalServer->arg("ssid");
    String pass   = g_portalServer->arg("pass");
    String ip     = g_portalServer->arg("ip");
    String port   = g_portalServer->arg("port");
    String player = g_portalServer->arg("player");
    String tz     = g_portalServer->arg("tz");
    String tz2    = g_portalServer->arg("tz2");

    if (ssid.isEmpty() || ip.isEmpty()) {
        g_portalServer->send(400, "text/plain", "SSID and LMS IP are required.");
        return;
    }

    strlcpy(appCfg.wifi_ssid,  ssid.c_str(), sizeof(appCfg.wifi_ssid));
    if (pass.length() > 0)
        strlcpy(appCfg.wifi_password, pass.c_str(), sizeof(appCfg.wifi_password));
    strlcpy(appCfg.lms_ip,     ip.c_str(),     sizeof(appCfg.lms_ip));
    appCfg.lms_port = (port.toInt() > 0) ? port.toInt() : 9000;
    strlcpy(appCfg.lms_player, player.c_str(), sizeof(appCfg.lms_player));
    strlcpy(appCfg.timezone,   tz.c_str(),     sizeof(appCfg.timezone));
    strlcpy(appCfg.timezone2,  tz2.c_str(),    sizeof(appCfg.timezone2));
    String cs = g_portalServer->arg("clock_style");
    if (cs == "analog" || cs == "digital" || cs == "casio")
        strlcpy(appCfg.clock_style, cs.c_str(), sizeof(appCfg.clock_style));

    if (saveAppConfig(appCfg)) {
        g_portalServer->send(200, "text/html; charset=utf-8", FPSTR(PORTAL_SAVED));
        delay(2000);
        ESP.restart();
    } else {
        g_portalServer->send(500, "text/plain", "Failed to save config to flash.");
    }
}

static void stopPortal() {
    if (!g_portalServer) return;
    g_dnsServer.stop();
    g_portalServer->stop();
    delete g_portalServer;
    g_portalServer = nullptr;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

static void startPortal() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("LMS-CYD-Config");

    // DNS captif : toutes les requêtes → 192.168.4.1
    // Déclenche la popup "portail captif" sur Android / iOS
    g_dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

    g_portalServer = new WebServer(80);
    g_portalServer->on("/",     HTTP_GET,  portalHandleRoot);
    g_portalServer->on("/save", HTTP_POST, portalHandleSave);
    // Captive portal : toute URL inconnue → formulaire
    g_portalServer->onNotFound([]() {
        g_portalServer->sendHeader("Location", "http://192.168.4.1/");
        g_portalServer->send(302, "text/plain", "");
    });
    g_portalServer->begin();
    drawPortalScreen();
}

// =============================================================================
//  Écran informations LMS Server
// =============================================================================
static void drawInfoServerScreen() {
    display.fillScreen(C_BG);
    drawSidebar();
    display.fillRect(SIDEBAR_W, 0, SCREEN_W - SIDEBAR_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("LMS Server Info", SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2, 6);
    display.drawFastHLine(SIDEBAR_W, HDR_H, SCREEN_W - SIDEBAR_W, C_SEPARATOR);
    display.setTextDatum(lgfx::top_left);

    display.setFont(&fonts::Font2);
    const int COL2 = SIDEBAR_W + 120;
    int y = 38;

    auto row = [&](const char* label, const String& val, uint32_t color) {
        display.setTextColor(C_FORMAT, C_BG);
        display.drawString(label, SIDEBAR_W + MARGIN, y);
        display.setTextColor(color, C_BG);
        display.drawString(val.c_str(), COL2, y);
        y += 20;
    };

    row("IP address", String(appCfg.lms_ip), C_TITLE);
    row("Port",       String(appCfg.lms_port), C_TITLE);
    y += 4;
    if (serverStatus.valid) {
        row("LMS version",  serverStatus.version,                   C_ALBUM);
        row("Players",      String(serverStatus.playerCount),        C_TITLE);
        row("Albums",       String(serverStatus.totalAlbums),        C_TITLE);
        row("Songs",        String(serverStatus.totalSongs),         C_TITLE);
    } else {
        display.setTextColor(0xFF4040u, C_BG);
        display.drawString("Server unreachable", SIDEBAR_W + MARGIN, y);
    }

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Tap sidebar to navigate", SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2, 225);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran informations Players
// =============================================================================
static void drawInfoPlayersScreen() {
    display.fillScreen(C_BG);
    drawSidebar();
    display.fillRect(SIDEBAR_W, 0, SCREEN_W - SIDEBAR_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Players Info", SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2, 6);
    display.drawFastHLine(SIDEBAR_W, HDR_H, SCREEN_W - SIDEBAR_W, C_SEPARATOR);
    display.setTextDatum(lgfx::top_left);

    static const int MAX_PLY = 5;
    PlayerInfo players[MAX_PLY];
    int count = lms.getPlayersInfo(players, MAX_PLY);

    if (count == 0) {
        display.setFont(&fonts::Font2);
        display.setTextColor(0xFF4040u, C_BG);
        display.setTextDatum(lgfx::top_center);
        display.drawString("No players found", SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2, 100);
        display.setTextDatum(lgfx::top_left);
    } else {
        int y = 34;
        for (int i = 0; i < count; i++) {
            uint32_t nc = players[i].connected ? C_PLAY_ICON : C_FORMAT;

            display.setFont(&fonts::FreeSans9pt7b);
            display.setTextColor(nc, C_BG);
            String name = players[i].name;
            if (display.textWidth(name.c_str()) > SCREEN_W - SIDEBAR_W - 2 * MARGIN)
                name = name.substring(0, 20) + "...";
            display.drawString(name.c_str(), SIDEBAR_W + MARGIN, y);
            y += 16;

            display.setFont(&fonts::Font2);
            display.setTextColor(C_FORMAT, C_BG);
            display.drawString(("IP:  " + (players[i].ip.length() > 0 ? players[i].ip : "--")).c_str(),
                               SIDEBAR_W + MARGIN + 6, y); y += 13;
            display.drawString(("MAC: " + players[i].playerid).c_str(),
                               SIDEBAR_W + MARGIN + 6, y); y += 13;
            if (players[i].firmware.length() > 0) {
                display.drawString(("FW:  " + players[i].firmware).c_str(),
                                   SIDEBAR_W + MARGIN + 6, y); y += 13;
            }
            y += 4;
            if (y > 210) break;
            display.drawFastHLine(SIDEBAR_W + MARGIN, y - 2, SCREEN_W - SIDEBAR_W - 2 * MARGIN, C_SEPARATOR);
        }
    }

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Tap sidebar to navigate", SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2, 225);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Horloge style G-SHOCK DW-5600 (7 segments)
// =============================================================================
// Encodage 7 segments : bit0=a(haut) bit1=b(haut-D) bit2=c(bas-D)
//                       bit3=d(bas)  bit4=e(bas-G)  bit5=f(haut-G) bit6=g(mid)
static const uint8_t SEG7_DIGITS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static void drawSeg7(int x, int y, int W, int H, int T,
                     uint8_t segs, uint32_t onC, uint32_t offC) {
    int hh = H / 2;
    display.fillRect(x + T,     y,            W - 2*T, T,      (segs & 0x01) ? onC : offC); // a haut
    display.fillRect(x + W - T, y + T,        T,       hh - T, (segs & 0x02) ? onC : offC); // b haut-D
    display.fillRect(x + W - T, y + hh,       T,       hh - T, (segs & 0x04) ? onC : offC); // c bas-D
    display.fillRect(x + T,     y + H - T,    W - 2*T, T,      (segs & 0x08) ? onC : offC); // d bas
    display.fillRect(x,         y + hh,       T,       hh - T, (segs & 0x10) ? onC : offC); // e bas-G
    display.fillRect(x,         y + T,        T,       hh - T, (segs & 0x20) ? onC : offC); // f haut-G
    display.fillRect(x + T,     y + hh - T/2, W - 2*T, T,      (segs & 0x40) ? onC : offC); // g mid
}

// fullRedraw=true  : dessine fond + éléments statiques (boîtier, bordure LCD…)
// fullRedraw=false : met à jour uniquement les éléments dynamiques (heure)
static void drawGShockClock(bool fullRedraw) {
    // Palette fidèle au DW-5600 : boîtier noir pur, LCD gris clair positif
    static const uint32_t C_GBODY = 0x080808u;  // boîtier quasi-noir
    static const uint32_t C_GLCD  = 0xC2CDB5u;  // fond LCD gris-vert clair (positif)
    static const uint32_t C_GON   = 0x141E08u;  // segment allumé (vert très sombre)
    static const uint32_t C_GOFF  = 0xE5E7D9u;  // segment éteint (LCD gris clair visible)
    static const uint32_t C_GBORD = 0x040404u;  // liseré LCD

    // 18px de boîtier en haut pour "PROTECTION", LCD de y=18 à y=214
    static const int LX = 10, LY = 18, LW = 300, LH = 196;

    struct tm t;
    bool hasTime = getLocalTime(&t, 10);

    if (fullRedraw) {
        // Boîtier noir (zone au-dessus de la nav bar)
        display.fillRect(0, 0, SCREEN_W, SCREEN_H - 22, C_GBODY);

        // "PROTECTION" gravé dans le boîtier en haut
        display.setFont(&fonts::Font0);
        display.setTextColor(0x363636u, C_GBODY);
        display.setTextDatum(lgfx::top_center);
        display.drawString("PROTECTION", SCREEN_W / 2, 5);
        display.setTextDatum(lgfx::top_left);

        // Panneau LCD avec coins arrondis (fidèle au DW-5600)
        display.fillRoundRect(LX,     LY,     LW,     LH,     8, C_GBORD);
        display.fillRoundRect(LX + 2, LY + 2, LW - 4, LH - 4, 6, C_GLCD);

        // Ligne de séparation de la bande info
        display.drawFastHLine(LX + 6, LY + 28, LW - 12, C_GOFF);
    }

    if (!hasTime) return;

    // ── Bande info supérieure ───────────────────────────────────────────────
    // Gauche : jour + indicateur AM/PM (comme "S U" + "PM" sur la vraie montre)
    // Droite : heure au format 12h (comme "6:30" sur la vraie montre)
    static const char* DAYS[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
    bool isPM = (t.tm_hour >= 12);
    int  h12  = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    char h12buf[6];
    snprintf(h12buf, sizeof(h12buf), "%d:%02d", h12, t.tm_min);

    display.setFont(&fonts::Font2);
    display.setTextColor(C_GON, C_GLCD);
    display.setTextDatum(lgfx::top_left);
    display.drawString(DAYS[t.tm_wday], LX + 6, LY + 4);
    display.setFont(&fonts::Font0);
    display.drawString(isPM ? "PM" : "AM", LX + 6, LY + 17);
    display.setFont(&fonts::Font2);
    display.setTextDatum(lgfx::top_right);
    display.drawString(h12buf, LX + LW - 6, LY + 4);
    display.setTextDatum(lgfx::top_left);

    // ── Heure principale HH:MM (grands 7 segments très gras) ───────────────
    // DW=54 DH=100 DT=11 | gap=6 | colon=16
    // Largeur totale : 54+6+54+16+54+6+54 = 244 px
    // LCD inner width = LW-4=296, dx = LX+2+(296-244)/2 = 12+26 = 38
    const int DW = 54, DH = 100, DT = 11, DG = 6, CW = 16;
    const int dx = LX + 2 + (LW - 4 - (DW + DG + DW + CW + DW + DG + DW)) / 2;  // 38
    const int dy = LY + 32;  // 50

    drawSeg7(dx,                            dy, DW, DH, DT, SEG7_DIGITS[t.tm_hour / 10], C_GON, C_GOFF);
    drawSeg7(dx + DW + DG,                  dy, DW, DH, DT, SEG7_DIGITS[t.tm_hour % 10], C_GON, C_GOFF);
    // Deux-points : cercles remplis (plus fidèle au DW-5600 que des carrés)
    int colCX = dx + DW + DG + DW + CW / 2;  // 38+54+6+54+8 = 160 = centre écran
    display.fillCircle(colCX, dy + DH / 3,       7, C_GON);
    display.fillCircle(colCX, dy + 2 * DH / 3,   7, C_GON);
    drawSeg7(dx + DW + DG + DW + CW,        dy, DW, DH, DT, SEG7_DIGITS[t.tm_min / 10], C_GON, C_GOFF);
    drawSeg7(dx + DW + DG + DW + CW + DW + DG, dy, DW, DH, DT, SEG7_DIGITS[t.tm_min % 10], C_GON, C_GOFF);

    // ── Secondes (petits 7 segments) ───────────────────────────────────────
    const int SW = 24, SH = 38, ST = 5, SG = 4;
    const int sy = dy + DH + 7;  // 157
    const int sx = SCREEN_W / 2 - (SW + SG + SW) / 2;
    drawSeg7(sx,           sy, SW, SH, ST, SEG7_DIGITS[t.tm_sec / 10], C_GON, C_GOFF);
    drawSeg7(sx + SW + SG, sy, SW, SH, ST, SEG7_DIGITS[t.tm_sec % 10], C_GON, C_GOFF);

    // ── "CASIO" à l'intérieur du LCD (comme sur la vraie montre) ───────────
    display.setFont(&fonts::Font2);
    display.setTextColor(C_GON, C_GLCD);
    display.setTextDatum(lgfx::top_center);
    display.drawString("CASIO", SCREEN_W / 2, sy + SH + 4);  // ~199
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran horloge sélectionnable (avec bouton NEXT pour cycler les styles)
// =============================================================================
static void drawClockScreen() {
    bool isAnalog = (strcmp(appCfg.clock_style, "analog") == 0);
    bool isCasio  = (strcmp(appCfg.clock_style, "casio")  == 0);
    bool hasTz2   = (appCfg.timezone2[0] != '\0');

    if (clockNeedsFullRedraw) {
        display.fillScreen(C_BG);
        drawSidebar();
        clockNeedsFullRedraw = false;

        if (isCasio) {
            drawGShockClock(true);
        }

        // Barre inférieure (commune à tous les styles)
        display.drawFastHLine(SIDEBAR_W, SCREEN_H - 22, SCREEN_W - SIDEBAR_W, C_SEPARATOR);
        display.setFont(&fonts::Font2);
        display.setTextColor(C_FORMAT, C_BG);
        display.setTextDatum(lgfx::top_left);
        display.drawString(isAnalog ? "Analog" : (isCasio ? "G-Shock" : "Digital"), SIDEBAR_W + MARGIN, SCREEN_H - 16);
        display.setTextColor(C_ALBUM, C_BG);
        display.setTextDatum(lgfx::top_right);
        display.drawString("Tap style >", SCREEN_W - MARGIN, SCREEN_H - 16);
        display.setTextDatum(lgfx::top_left);
    }

    if (isCasio) {
        drawGShockClock(false);
        return;
    }

    struct tm t;
    bool hasTime = getLocalTime(&t, 10);

    int contentCenterX = SIDEBAR_W + (SCREEN_W - SIDEBAR_W) / 2;

    if (isAnalog) {
        if (hasTime) {
            if (clockPrevValid)
                drawAnalogClockHands(clockPrevT, C_BG, C_BG); // efface anciennes aiguilles
            drawAnalogClockFace();                              // restaure le cadran
            drawAnalogClockHands(t, C_TITLE, C_VOLUME);        // dessine les nouvelles
            clockPrevT = t;
            clockPrevValid = true;

            char dateBuf[12];
            strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &t);
            display.setFont(&fonts::Font2);
            display.setTextColor(C_FORMAT, C_BG);
            display.setTextDatum(lgfx::top_center);
            display.drawString(dateBuf, contentCenterX, hasTz2 ? 186 : 196);

            if (hasTz2) {
                struct tm t2;
                if (getTimeInZone(appCfg.timezone2, t2)) {
                    char tz2Buf[32];
                    strftime(tz2Buf, sizeof(tz2Buf), "%H:%M:%S", &t2);
                    char tz2Line[48];
                    snprintf(tz2Line, sizeof(tz2Line), "%s  %s",
                             tzCityName(appCfg.timezone2).c_str(), tz2Buf);
                    display.setTextColor(C_ALBUM, C_BG);
                    display.drawString(tz2Line, contentCenterX, 202);
                }
            }
            display.setTextDatum(lgfx::top_left);
        }
        if (!hasTz2) {
            display.setFont(&fonts::Font0);
            display.setTextDatum(lgfx::top_center);
            if (serverStatus.valid) {
                char buf[64];
                display.setTextColor(C_FORMAT, C_BG);
                snprintf(buf, sizeof(buf), "LMS v%s  —  %d players  %d albums  %d songs",
                         serverStatus.version.c_str(), serverStatus.playerCount,
                         serverStatus.totalAlbums, serverStatus.totalSongs);
                display.drawString(buf, contentCenterX, 208);
            } else {
                display.setTextColor(0xFF4040u, C_BG);
                display.drawString((String("LMS unreachable — ") + appCfg.lms_ip).c_str(),
                                   contentCenterX, 208);
            }
            display.setTextDatum(lgfx::top_left);
        }
    } else {
        if (hasTime) {
            char timeBuf[9], dateBuf[12];
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
            strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &t);
            display.setFont(&fonts::FreeSans24pt7b);
            display.setTextColor(C_CLOCK, C_BG);
            display.setTextDatum(lgfx::top_center);
            display.drawString(timeBuf, contentCenterX, hasTz2 ? 2 : 30);
            if (hasTz2) {
                display.setFont(&fonts::Font2);
            } else {
                display.setFont(&fonts::FreeSans9pt7b);
            }
            display.setTextColor(C_FORMAT, C_BG);
            display.drawString(dateBuf, contentCenterX, hasTz2 ? 42 : 100);

            if (hasTz2) {
                display.drawFastHLine(SIDEBAR_W + MARGIN, 62, SCREEN_W - SIDEBAR_W - 2 * MARGIN, C_SEPARATOR);
                display.setFont(&fonts::Font2);
                display.setTextColor(C_FORMAT, C_BG);
                display.drawString(tzCityName(appCfg.timezone2).c_str(), contentCenterX, 66);

                struct tm t2;
                if (getTimeInZone(appCfg.timezone2, t2)) {
                    char tz2Time[9], tz2Date[12];
                    strftime(tz2Time, sizeof(tz2Time), "%H:%M:%S", &t2);
                    strftime(tz2Date, sizeof(tz2Date), "%d/%m/%Y", &t2);
                    display.setFont(&fonts::FreeSans18pt7b);
                    display.setTextColor(C_CLOCK, C_BG);
                    display.drawString(tz2Time, contentCenterX, 84);
                    display.setFont(&fonts::Font2);
                    display.setTextColor(C_FORMAT, C_BG);
                    display.drawString(tz2Date, contentCenterX, 115);
                }
            }
            display.setTextDatum(lgfx::top_left);
        }
        display.setFont(&fonts::Font2);
        display.setTextDatum(lgfx::top_left);
        if (serverStatus.valid) {
            char buf[48];
            display.setTextColor(C_FORMAT, C_BG);
            snprintf(buf, sizeof(buf), "LMS v%s", serverStatus.version.c_str());
            display.drawString(buf, SIDEBAR_W + MARGIN, hasTz2 ? 140 : 135);
            snprintf(buf, sizeof(buf), "Players: %d  Albums: %d  Songs: %d",
                     serverStatus.playerCount, serverStatus.totalAlbums, serverStatus.totalSongs);
            display.drawString(buf, SIDEBAR_W + MARGIN, hasTz2 ? 156 : 155);
        } else {
            display.setTextColor(0xFF4040u, C_BG);
            display.drawString((String("LMS unreachable — ") + appCfg.lms_ip).c_str(), SIDEBAR_W + MARGIN, hasTz2 ? 140 : 135);
        }

        if (hasTz2 && playerStatus.valid && playerStatus.isPlaying) {
            String pname = playerStatus.playerName;
            if (pname.length() > 16) pname = pname.substring(0, 16);
            String album = trackInfo.valid ? trackInfo.album : "";
            if (album.isEmpty()) album = playerStatus.currentTitle;
            if (album.length() > 20) album = album.substring(0, 20);
            char buf[64];
            snprintf(buf, sizeof(buf), "%-16s reading: %s", pname.c_str(), album.c_str());
            display.setFont(&fonts::Font0);
            display.setTextColor(C_TITLE, C_BG);
            display.drawString(buf, SIDEBAR_W + MARGIN, 172);
        } else if (hasTz2) {
            // Effacer la ligne si plus en lecture
            display.fillRect(SIDEBAR_W, 172, SCREEN_W - SIDEBAR_W, 10, C_BG);
        }
    }
}

static void cycleClockStyle() {
    if (strcmp(appCfg.clock_style, "digital") == 0)
        strlcpy(appCfg.clock_style, "analog", sizeof(appCfg.clock_style));
    else if (strcmp(appCfg.clock_style, "analog") == 0)
        strlcpy(appCfg.clock_style, "casio", sizeof(appCfg.clock_style));
    else
        strlcpy(appCfg.clock_style, "digital", sizeof(appCfg.clock_style));
    saveAppConfig(appCfg);
    clockNeedsFullRedraw = true;
    drawClockScreen();
}

// =============================================================================
//  Navigation entre écrans
// =============================================================================
static void enterScreen(Screen s) {
    if (currentScreen == SCR_PORTAL && s != SCR_PORTAL) stopPortal();
    currentScreen = s;
    switch (s) {
        case SCR_MAIN:
            lastPoll             = 0;   // poll LMS immédiatement
            fullRedrawNeeded     = true;
            lastIsPlaying        = false;
            clockNeedsFullRedraw = true;
            clockPrevValid       = false;  // reset pour prochain entrée clock
            display.fillScreen(C_BG);
            break;
        case SCR_CLOCK:
            lastClock            = 0;   // forcer le dessin immédiat dans loop()
            clockNeedsFullRedraw = true;
            clockPrevValid       = false;  // force premier dessin sans effacer
            drawClockScreen();
            break;
        case SCR_INFO_SRV:
            drawInfoServerScreen();
            break;
        case SCR_INFO_PLY:
            drawInfoPlayersScreen();
            break;
        case SCR_PORTAL:
            startPortal();
            break;
    }
}

// =============================================================================
//  Gestion des taps courts (selon l'écran courant)
// =============================================================================
static void handleShortTap(int16_t tx, int16_t ty) {
    // Sidebar : côté gauche visuel = tx élevé (axe X physiquement inversé)
    if (currentScreen != SCR_PORTAL && tx > SCREEN_W - SIDEBAR_W) {
        int btn = ty / SIDEBAR_BTN_H;
        switch (btn) {
            case 0: enterScreen(SCR_MAIN);     break;
            case 1: enterScreen(SCR_CLOCK);    break;
            case 2: enterScreen(SCR_INFO_SRV); break;
            case 3: enterScreen(SCR_INFO_PLY); break;
            case 4: enterScreen(SCR_PORTAL);   break;
        }
        return;
    }

    switch (currentScreen) {
        case SCR_MAIN: {
            if (!playerStatus.valid || playerStatus.playerid.isEmpty()) return;

            // Axe X inversé : gauche visuel = tx élevé, droite visuel = tx bas.
            // Zone gauche (next) = tx élevé, zone droite (prev) = tx bas.
            int ctrlW = SCREEN_W - SIDEBAR_W;
            if (tx > ctrlW * 2 / 3) {
                lms.nextTrack(playerStatus.playerid);
            } else if (tx > ctrlW / 3) {
                if (playerStatus.isPlaying) lms.pause(playerStatus.playerid);
                else                        lms.play(playerStatus.playerid);
            } else {
                lms.prevTrack(playerStatus.playerid);
            }
            lastPoll = 0;
            break;
        }

        case SCR_CLOCK:
            cycleClockStyle();
            break;

        case SCR_INFO_SRV:
        case SCR_INFO_PLY:
            enterScreen(SCR_MAIN);
            break;

        case SCR_PORTAL:
            break;  // touches ignorées (l'utilisateur navigue depuis son téléphone)
    }
}

// =============================================================================
//  Gestion du tactile
//
//  Machine d'états à 3 phases pour tolérer les parasites XPT2046 :
//    TS_IDLE        : pas de contact
//    TS_PRESSING    : contact en cours, accumulation du temps
//    TS_PENDING_TAP : contact relâché, on attend TOUCH_GLITCH_MS avant de
//                     confirmer le tap (si le contact revient → c'était un glitch)
// =============================================================================
enum TouchState { TS_IDLE, TS_PRESSING, TS_PENDING_TAP };
static TouchState    touchState  = TS_IDLE;
static unsigned long touchDownMs = 0;   // quand le contact a commencé
static unsigned long touchUpMs   = 0;   // quand le contact a été perdu
static unsigned long lastTouchMs = 0;   // dernier tap confirmé (anti-rebond)
static int16_t       lastTx      = 0;
static int16_t       lastTy      = 0;

static void handleTouch() {
    int16_t tx, ty;
    bool pressed = display.getTouch(&tx, &ty);
    unsigned long now = millis();

    switch (touchState) {
        case TS_IDLE:
            if (pressed) {
                touchDownMs = now;
                touchState  = TS_PRESSING;
                lastTx = tx;  lastTy = ty;
            }
            break;

        case TS_PRESSING:
            if (pressed) {
                lastTx = tx;  lastTy = ty;
            } else {
                // Contact perdu : glitch ou vrai relâché ?
                touchUpMs  = now;
                touchState = TS_PENDING_TAP;
            }
            break;

        case TS_PENDING_TAP:
            if (pressed) {
                // Contact revenu dans la fenêtre → c'était un glitch, on reprend
                touchState = TS_PRESSING;
                lastTx = tx;  lastTy = ty;
            } else if (now - touchUpMs >= TOUCH_GLITCH_MS) {
                // Vrai relâché : confirmer le tap court si applicable
                unsigned long held = touchUpMs - touchDownMs;
                if (held < LONG_PRESS_MS && now - lastTouchMs >= TOUCH_DEBOUNCE_MS) {
                    lastTouchMs = now;
                    handleShortTap(lastTx, lastTy);
                }
                touchState = TS_IDLE;
            }
            break;
    }
}

// =============================================================================
//  Connexion WiFi
// =============================================================================
static void connectWifi() {
    drawConnecting("Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(appCfg.wifi_ssid, appCfg.wifi_password);

    unsigned long start = millis();
    int dot = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 20000) {
            drawConnecting("WiFi: failed, retrying...");
            delay(2000);
            return;
        }
        if ((dot++ % 6) == 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "SSID: %s  %ds", appCfg.wifi_ssid,
                     (int)((millis() - start) / 1000));
            drawConnecting(buf);
        }
        delay(500);
    }

    // NTP pour l'horloge de veille (fuseau horaire local)
    configTzTime(ianaToposix(appCfg.timezone), "pool.ntp.org", "time.nist.gov");
    drawConnecting("WiFi OK  —  connecting LMS...");
    delay(500);
}

// =============================================================================
//  Découverte LMS par scan TCP du sous-réseau /24
//  Fast path : si l'IP configurée répond déjà, rien à faire.
//  Slow path : scan de toutes les IPs du /24 sur le port LMS.
// =============================================================================
static void discoverLmsViaScan() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Fast path : l'IP configurée est toujours valide
    if (lms.getServerStatus().valid) return;

    drawConnecting("Scanning network for LMS...");

    IPAddress local = WiFi.localIP();
    uint32_t base = ((uint32_t)local[0] << 24) | ((uint32_t)local[1] << 16)
                  | ((uint32_t)local[2] << 8);   // /24 prefix en big-endian

    for (int i = 1; i < 255; i++) {
        IPAddress candidate(local[0], local[1], local[2], i);
        if (candidate == local) continue;

        WiFiClient client;
        if (!client.connect(candidate, appCfg.lms_port, 100)) continue;
        client.stop();

        // Port ouvert — vérifier que c'est bien LMS
        String ip = candidate.toString();
        lms.init(ip.c_str(), appCfg.lms_port);
        if (!lms.getServerStatus().valid) continue;

        // Trouvé
        if (ip != String(appCfg.lms_ip)) {
            char msg[48];
            snprintf(msg, sizeof(msg), "LMS found: %s", ip.c_str());
            drawConnecting(msg);
            delay(1200);
            strlcpy(appCfg.lms_ip, ip.c_str(), sizeof(appCfg.lms_ip));
            saveAppConfig(appCfg);
        }
        return;
    }

    // Non trouvé — restaurer l'IP configurée pour le comportement habituel
    lms.init(appCfg.lms_ip, appCfg.lms_port);
}

// =============================================================================
//  Setup
// =============================================================================
void setup() {
    Serial.begin(115200);

    display.init();
    display.setRotation(1);   // paysage, USB vers la droite
    display.setBrightness(DISPLAY_BRIGHTNESS);
    display.fillScreen(C_BG);

    drawConnecting("Loading config...");
    delay(300);

    if (!loadAppConfig(appCfg)) {
        display.fillScreen(C_BG);
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(0xFF4040u, C_BG);
        display.setTextDatum(lgfx::top_center);
        display.drawString("Config error!", SCREEN_W / 2, 60);
        display.setFont(&fonts::Font2);
        display.setTextColor(C_FORMAT, C_BG);
        display.drawString("Edit data/config.json", SCREEN_W / 2, 100);
        display.drawString("then flash with:", SCREEN_W / 2, 120);
        display.drawString("pio run -t uploadfs", SCREEN_W / 2, 140);
        display.setTextDatum(lgfx::top_left);
        while (true) delay(1000);
    }

    lms.init(appCfg.lms_ip, appCfg.lms_port);

    connectWifi();
    discoverLmsViaScan();

    serverStatus = lms.getServerStatus();
    drawStartup();
}

// =============================================================================
//  Boucle principale
// =============================================================================
void loop() {
    unsigned long now = millis();

    // --- Gestion du tactile (tous écrans) ---
    handleTouch();

    // --- Portail web : traiter les requêtes HTTP ---
    if (currentScreen == SCR_PORTAL) {
        g_dnsServer.processNextRequest();
        if (g_portalServer) g_portalServer->handleClient();
        delay(5);
        return;
    }

    // --- Écran horloge : rafraîchir chaque seconde (mais on continue pour poller LMS) ---
    if (currentScreen == SCR_CLOCK) {
        if (now - lastClock > 1000) {
            lastClock = now;
            drawClockScreen();
        }
    }

    // --- Écrans de menu statiques : rien à faire ---
    if (currentScreen != SCR_MAIN && currentScreen != SCR_CLOCK) {
        delay(20);
        return;
    }

    // --- Reconnexion WiFi automatique ---
    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastPoll > 10000) {
            lastPoll = now;
            drawConnecting("WiFi lost, reconnecting...");
            WiFi.reconnect();
        }
        return;
    }

    // --- Interrogation du serveur LMS (polling adaptatif) ---
    {
        float  _inf  = inferredElapsed();
        float  _rem  = (trackInfo.valid && trackInfo.duration > 0)
                       ? trackInfo.duration - _inf : -1.0f;
        bool   _near = !playerStatus.isPlaying
                    || _inf < POLL_BOUNDARY_S
                    || (_rem >= 0 && _rem < POLL_BOUNDARY_S);
        if (now - lastPoll >= (_near ? POLL_FAST_MS : POLL_MID_MS)) {
        lastPoll = now;

        static unsigned long lastServerPoll = 0;
        if (now - lastServerPoll > 60000 || !serverStatus.valid) {
            lastServerPoll = now;
            ServerStatus newStatus = lms.getServerStatus();
            if (newStatus.valid) {
                serverStatus = newStatus;
                serverStatusAge = now;
            } else if (serverStatusAge > 0 && now - serverStatusAge > 120000) {
                // Dernier succès > 2 min → on invalide pour afficher "unreachable"
                serverStatus.valid = false;
            }
        }

        PlayerStatus newPlayer;
        bool found = lms.findPlayer(appCfg.lms_player, newPlayer);

        if (found && newPlayer.isPlaying) {
            // songChanged OU données manquantes (fetch raté lors du démarrage / LMS indisponible)
            bool songChanged = (newPlayer.currentSongId != lastSongId)
                            || (!trackInfo.valid && newPlayer.currentSongId > 0)
                            || (g_coverBuf == nullptr && g_coverSongId == newPlayer.currentSongId);

            if (songChanged) {
                downloadCover(newPlayer.currentSongId);
                trackInfo = lms.getSongInfo(newPlayer.currentSongId, newPlayer.playerid);
                scrollArtist.reset();
                scrollAlbum.reset();
                scrollTitle.reset();
                lastArtistOffset  = -1;
                lastAlbumOffset   = -1;
                lastTitleOffset   = -1;
                lastSongId        = newPlayer.currentSongId;
                fullRedrawNeeded  = true;
                drawFormatInfo();
            }

            if (!playerStatus.valid || !lastIsPlaying) {
                fullRedrawNeeded = true;
            }

            playerStatus      = newPlayer;
            g_elapsedAnchor   = newPlayer.elapsed;
            g_elapsedAnchorMs = now;
            if (currentScreen == SCR_MAIN) {
                drawPlayingScreen(fullRedrawNeeded);
                fullRedrawNeeded = false;
            }

        } else if (found) {
            // Player trouvé mais pas en lecture (pause/stop) → on met à jour le statut
            // mais on reste sur l'écran actuel (plus de transition auto vers clock)
            playerStatus = newPlayer;
            if (currentScreen == SCR_MAIN) {
                lastSongId    = -1;
                lastIsPlaying = false;
                drawPlayingScreen(true);
            }
        }
        // else: found == false (erreur réseau ou player disparu) → on garde l'affichage actuel
        } // if (now - lastPoll >= interval)
    }

    // --- Défilement du texte et barre de progression (rafraîchissement local) ---
    if (currentScreen == SCR_MAIN && playerStatus.valid && playerStatus.isPlaying && !fullRedrawNeeded) {
        drawPlayingScreen(false);
    }

    delay(20);  // ~50 fps max
}
