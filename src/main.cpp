//  LMS CYD — Logitech Media Server → ESP32 Cheap Yellow Display (ESP32-2432S028)

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <time.h>

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
enum Screen { SCR_MAIN, SCR_HOME, SCR_CLOCK, SCR_INFO_SRV, SCR_INFO_PLY, SCR_PORTAL };
static Screen currentScreen = SCR_MAIN;

// =============================================================================
//  Zones de l'écran (paysage 320×240)
// =============================================================================
static const int SCREEN_W   = 320;
static const int SCREEN_H   = 240;
static const int MARGIN     = 6;

// En-tête (pleine largeur)
static const int HDR_Y      = 0;
static const int HDR_H      = 28;

// Pochette d'album (colonne gauche)
static const int ART_X      = 0;
static const int ART_Y      = HDR_H;        // 28
static const int ART_W      = 120;
static const int ART_H      = 120;          // se termine à y=148

// Infos texte (colonne droite, à côté de la pochette)
static const int INFO_X     = ART_W + 4;   // 124
static const int INFO_W     = SCREEN_W - INFO_X;  // 196
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

static const int TEXT_W     = SCREEN_W - 2 * MARGIN;  // pour progress/format

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

static bool      clockNeedsFullRedraw = true;
static struct tm clockPrevT           = {};

// Web portal
static WebServer* g_portalServer = nullptr;
static DNSServer  g_dnsServer;

// Cache de la pochette courante
static uint8_t* g_coverBuf    = nullptr;
static size_t   g_coverLen    = 0;
static int      g_coverSongId = -2;   // -2 = jamais chargé

// =============================================================================
//  Pochette d'album
// =============================================================================
static void downloadCover(int songId) {
    if (g_coverSongId == songId) return;   // déjà en cache

    if (g_coverBuf) { free(g_coverBuf); g_coverBuf = nullptr; g_coverLen = 0; }
    g_coverSongId = songId;

    if (songId <= 0 || WiFi.status() != WL_CONNECTED) return;

    String url = String("http://") + appCfg.lms_ip + ":" + appCfg.lms_port
               + "/music/" + songId + "/cover_" + ART_W + "x" + ART_H + "_p";

    WiFiClient wifiClient;
    HTTPClient http;
    http.begin(wifiClient, url);
    http.setTimeout(4000);

    if (http.GET() != 200) { http.end(); return; }

    int contentLen = http.getSize();
    const int MAX_COVER = 30720;   // 30 KB

    if (contentLen > MAX_COVER) { http.end(); return; }

    size_t allocLen = (contentLen > 0) ? (size_t)contentLen : (size_t)MAX_COVER;
    uint8_t* buf = (uint8_t*)malloc(allocLen);
    if (!buf) { http.end(); return; }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t t = millis();
    while (millis() - t < 4000 && total < allocLen && http.connected()) {
        int avail = stream->available();
        if (avail > 0) {
            int rd = stream->readBytes(buf + total,
                         (int)min((size_t)avail, allocLen - total));
            total += rd;
            if (contentLen > 0 && total >= (size_t)contentLen) break;
        }
        delay(1);
    }
    http.end();

    if (total > 0) {
        g_coverBuf = buf;
        g_coverLen = total;
    } else {
        free(buf);
    }
}

static void drawCover() {
    if (g_coverBuf && g_coverLen > 0) {
        display.drawJpg(g_coverBuf, g_coverLen, ART_X, ART_Y, ART_W, ART_H);
    } else {
        // Placeholder : carré sombre avec cadre
        display.fillRect(ART_X, ART_Y, ART_W, ART_H, C_TRACK_BG);
        display.drawRect(ART_X + 4, ART_Y + 4, ART_W - 8, ART_H - 8, C_SEPARATOR);
        display.setFont(&fonts::FreeSans12pt7b);
        display.setTextColor(C_SEPARATOR, C_TRACK_BG);
        display.setTextDatum(lgfx::middle_center);
        display.drawString("?", ART_X + ART_W / 2, ART_Y + ART_H / 2);
        display.setTextDatum(lgfx::top_left);
    }
    // Séparateur vertical pochette / texte
    display.drawFastVLine(ART_X + ART_W + 1, ART_Y, ART_H, C_SEPARATOR);
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

    display.fillRect(0, HDR_Y, SCREEN_W, HDR_H, C_HEADER_BG);

    // Icône play/pause
    drawStatusIcon(MARGIN, 5, playerStatus.isPlaying);

    // Nom du player (tronqué)
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_left);
    String name = playerStatus.playerName;
    if (name.length() > 16) name = name.substring(0, 15) + "…";
    display.drawString(name.c_str(), 30, 5);

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
    display.drawFastHLine(0, HDR_H,     SCREEN_W, C_SEPARATOR);
    display.drawFastHLine(0, HDR_H + 1, SCREEN_W, C_SEPARATOR >> 1);

    lastVolume    = playerStatus.volume;
    lastIsPlaying = playerStatus.isPlaying;
}

// =============================================================================
//  Dessin de la barre de progression + infos de piste
// =============================================================================
static void drawProgress(bool forceRedraw) {
    if (!forceRedraw && (int)playerStatus.elapsed == (int)lastElapsed) return;
    lastElapsed = playerStatus.elapsed;

    display.drawFastHLine(0, PROG_Y, SCREEN_W, C_SEPARATOR);

    // Temps écoulé / total
    String elapsed  = formatTime(playerStatus.elapsed);
    String duration = trackInfo.valid ? formatTime(trackInfo.duration) : "--:--";
    String pos      = elapsed + " / " + duration;

    display.setFont(&fonts::Font2);
    display.setTextColor(C_FORMAT, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.fillRect(0, PROG_Y + 2, SCREEN_W, 14, C_BG);
    display.drawString(pos.c_str(), MARGIN, PROG_Y + 3);

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
        ratio = playerStatus.elapsed / trackInfo.duration;
    drawBar(MARGIN, PROG_Y + 20, SCREEN_W - 2 * MARGIN, 10,
            ratio, C_PROGRESS, C_TRACK_BG);
}

// =============================================================================
//  Dessin des infos de format audio
// =============================================================================
static void drawFormatInfo() {
    display.fillRect(0, FMT_Y, SCREEN_W, FMT_H, C_BG);
    display.drawFastHLine(0, FMT_Y, SCREEN_W, C_SEPARATOR);

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
    display.drawString(left.c_str(), MARGIN, FMT_Y + 4);
}

// =============================================================================
//  Zone de contrôle (indications tactiles discrètes)
// =============================================================================
static void drawControls() {
    display.fillRect(0, CTRL_Y, SCREEN_W, CTRL_H, C_BG);
    display.drawFastHLine(0, CTRL_Y, SCREEN_W, C_SEPARATOR);

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.drawString("|<< PREV", MARGIN, CTRL_Y + 10);
    display.setTextDatum(lgfx::top_center);
    display.drawString("PLAY/PAUSE", SCREEN_W / 2, CTRL_Y + 10);
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

// Retourne l'heure locale dans un fuseau IANA quelconque.
// Change temporairement TZ puis restaure le fuseau principal.
static bool getTimeInZone(const char* iana, struct tm& t) {
    time_t now;
    time(&now);
    if (now < 1000000) return false;   // NTP pas encore synchronisé
    setenv("TZ", ianaToposix(iana), 1);
    tzset();
    localtime_r(&now, &t);
    setenv("TZ", ianaToposix(appCfg.timezone), 1);
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
    bool hasTime  = getLocalTime(&t);
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
            if (prevValid)
                drawAnalogClockHands(clockPrevT, C_BG, C_BG); // efface anciennes aiguilles
            drawAnalogClockFace();                              // restaure le cadran
            drawAnalogClockHands(t, C_TITLE, C_VOLUME);        // dessine les nouvelles
            clockPrevT = t;

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
    html.replace("%SEL_DIG%", isAnalog ? ""          : " selected");
    html.replace("%SEL_ANA%", isAnalog ? " selected" : "");
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
    if (cs == "analog" || cs == "digital")
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
//  Écran Home — menu de navigation
// =============================================================================
static void drawHomeScreen() {
    display.fillScreen(C_BG);

    // Header
    display.fillRect(0, 0, SCREEN_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("MENU", SCREEN_W / 2, 6);
    display.drawFastHLine(0, HDR_H, SCREEN_W, C_SEPARATOR);
    display.setTextDatum(lgfx::top_left);

    // 5 menu items — chaque bouton : ~42 px de hauteur (5×42 + 28 = 238 ≈ 240)
    const int      ITEM_H    = (SCREEN_H - HDR_H) / 5;   // 42
    const char*    labels[]  = { "Now Playing", "Clock", "LMS Server Info", "Players Info", "Web Portal" };
    const uint32_t accents[] = { C_PLAY_ICON,   C_CLOCK,  C_ALBUM,           C_ARTIST,       C_VOLUME };

    for (int i = 0; i < 5; i++) {
        int y = HDR_H + i * ITEM_H;
        int h = (i < 4) ? ITEM_H : (SCREEN_H - y);   // dernier item prend le reste
        display.fillRect(0, y, SCREEN_W, h, C_BG);
        display.fillRect(0, y + 2, 5, h - 4, accents[i]);   // accent gauche
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(accents[i], C_BG);
        display.setTextDatum(lgfx::middle_left);
        display.drawString(labels[i], 12, y + h / 2);
        display.setTextColor(C_FORMAT, C_BG);
        display.setTextDatum(lgfx::middle_right);
        display.drawString(">", SCREEN_W - MARGIN, y + h / 2);
        if (i < 4)
            display.drawFastHLine(5, y + h - 1, SCREEN_W - 5, C_SEPARATOR);
    }
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran informations LMS Server
// =============================================================================
static void drawInfoServerScreen() {
    display.fillScreen(C_BG);
    display.fillRect(0, 0, SCREEN_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("LMS Server Info", SCREEN_W / 2, 6);
    display.drawFastHLine(0, HDR_H, SCREEN_W, C_SEPARATOR);
    display.setTextDatum(lgfx::top_left);

    display.setFont(&fonts::Font2);
    const int COL2 = 120;
    int y = 38;

    auto row = [&](const char* label, const String& val, uint32_t color) {
        display.setTextColor(C_FORMAT, C_BG);
        display.drawString(label, MARGIN, y);
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
        display.drawString("Server unreachable", MARGIN, y);
    }

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Tap anywhere to go back", SCREEN_W / 2, 225);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran informations Players
// =============================================================================
static void drawInfoPlayersScreen() {
    display.fillScreen(C_BG);
    display.fillRect(0, 0, SCREEN_W, HDR_H, C_HEADER_BG);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TITLE, C_HEADER_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Players Info", SCREEN_W / 2, 6);
    display.drawFastHLine(0, HDR_H, SCREEN_W, C_SEPARATOR);
    display.setTextDatum(lgfx::top_left);

    static const int MAX_PLY = 5;
    PlayerInfo players[MAX_PLY];
    int count = lms.getPlayersInfo(players, MAX_PLY);

    if (count == 0) {
        display.setFont(&fonts::Font2);
        display.setTextColor(0xFF4040u, C_BG);
        display.setTextDatum(lgfx::top_center);
        display.drawString("No players found", SCREEN_W / 2, 100);
        display.setTextDatum(lgfx::top_left);
    } else {
        int y = 34;
        for (int i = 0; i < count; i++) {
            uint32_t nc = players[i].connected ? C_PLAY_ICON : C_FORMAT;

            display.setFont(&fonts::FreeSans9pt7b);
            display.setTextColor(nc, C_BG);
            String name = players[i].name;
            if (display.textWidth(name.c_str()) > SCREEN_W - 2 * MARGIN)
                name = name.substring(0, 20) + "...";
            display.drawString(name.c_str(), MARGIN, y);
            y += 16;

            display.setFont(&fonts::Font2);
            display.setTextColor(C_FORMAT, C_BG);
            display.drawString(("IP:  " + (players[i].ip.length() > 0 ? players[i].ip : "--")).c_str(),
                               MARGIN + 6, y); y += 13;
            display.drawString(("MAC: " + players[i].playerid).c_str(),
                               MARGIN + 6, y); y += 13;
            if (players[i].firmware.length() > 0) {
                display.drawString(("FW:  " + players[i].firmware).c_str(),
                                   MARGIN + 6, y); y += 13;
            }
            y += 4;
            if (y > 210) break;
            display.drawFastHLine(MARGIN, y - 2, SCREEN_W - 2 * MARGIN, C_SEPARATOR);
        }
    }

    display.setFont(&fonts::Font0);
    display.setTextColor(0x404040u, C_BG);
    display.setTextDatum(lgfx::top_center);
    display.drawString("Tap anywhere to go back", SCREEN_W / 2, 225);
    display.setTextDatum(lgfx::top_left);
}

// =============================================================================
//  Écran horloge sélectionnable (avec bouton NEXT pour cycler les styles)
// =============================================================================
static void drawClockScreen() {
    struct tm t;
    bool hasTime  = getLocalTime(&t);
    bool isAnalog = (strcmp(appCfg.clock_style, "analog") == 0);
    bool prevValid = !clockNeedsFullRedraw;
    bool hasTz2   = (appCfg.timezone2[0] != '\0');

    if (clockNeedsFullRedraw) {
        display.fillScreen(C_BG);
        clockNeedsFullRedraw = false;

        // Barre inférieure dessinée une seule fois (statique)
        display.drawFastHLine(0, SCREEN_H - 22, SCREEN_W, C_SEPARATOR);
        display.setFont(&fonts::Font2);
        display.setTextColor(C_FORMAT, C_BG);
        display.setTextDatum(lgfx::top_left);
        display.drawString(isAnalog ? "Analog" : "Digital", MARGIN, SCREEN_H - 16);
        display.setTextColor(C_ALBUM, C_BG);
        display.setTextDatum(lgfx::top_right);
        display.drawString("NEXT >", SCREEN_W - MARGIN, SCREEN_H - 16);
        display.setTextDatum(lgfx::top_left);
    }

    if (isAnalog) {
        if (hasTime) {
            if (prevValid)
                drawAnalogClockHands(clockPrevT, C_BG, C_BG); // efface anciennes aiguilles
            drawAnalogClockFace();                              // restaure le cadran
            drawAnalogClockHands(t, C_TITLE, C_VOLUME);        // dessine les nouvelles
            clockPrevT = t;

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
                    display.drawString(tz2Line, SCREEN_W / 2, 202);
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
                display.drawString(buf, SCREEN_W / 2, 208);
            } else {
                display.setTextColor(0xFF4040u, C_BG);
                display.drawString((String("LMS unreachable — ") + appCfg.lms_ip).c_str(),
                                   SCREEN_W / 2, 208);
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
    }
}

static void cycleClockStyle() {
    if (strcmp(appCfg.clock_style, "analog") == 0)
        strlcpy(appCfg.clock_style, "digital", sizeof(appCfg.clock_style));
    else
        strlcpy(appCfg.clock_style, "analog", sizeof(appCfg.clock_style));
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
            display.fillScreen(C_BG);
            break;
        case SCR_HOME:
            drawHomeScreen();
            break;
        case SCR_CLOCK:
            lastClock            = 0;   // forcer le dessin immédiat dans loop()
            clockNeedsFullRedraw = true;
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
    switch (currentScreen) {
        case SCR_MAIN:
            if (!playerStatus.valid || playerStatus.playerid.isEmpty()) return;

            // Zones : gauche = suivant | centre = play/pause | droite = précédent
            if (tx < SCREEN_W / 3) {
                lms.nextTrack(playerStatus.playerid);
            } else if (tx < 2 * SCREEN_W / 3) {
                if (playerStatus.isPlaying) lms.pause(playerStatus.playerid);
                else                        lms.play(playerStatus.playerid);
            } else {
                lms.prevTrack(playerStatus.playerid);
            }
            lastPoll = 0;
            break;

        case SCR_HOME: {
            if (ty < HDR_H) return;
            // 5 items de hauteur égale sur (SCREEN_H - HDR_H)
            int idx = constrain((ty - HDR_H) * 5 / (SCREEN_H - HDR_H), 0, 4);
            switch (idx) {
                case 0: enterScreen(SCR_MAIN);     break;
                case 1: enterScreen(SCR_CLOCK);    break;
                case 2: enterScreen(SCR_INFO_SRV); break;
                case 3: enterScreen(SCR_INFO_PLY); break;
                default: enterScreen(SCR_PORTAL);  break;
            }
            break;
        }

        case SCR_CLOCK:
            cycleClockStyle();
            break;

        case SCR_INFO_SRV:
        case SCR_INFO_PLY:
            enterScreen(SCR_HOME);
            break;

        case SCR_PORTAL:
            break;  // touches ignorées (l'utilisateur navigue depuis son téléphone)
    }
}

// =============================================================================
//  Gestion du tactile (long press → menu principal)
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
                // Long press → ouvrir le menu
                if (currentScreen != SCR_HOME && now - touchDownMs >= LONG_PRESS_MS) {
                    lastTouchMs = now;
                    touchState  = TS_IDLE;
                    enterScreen(SCR_HOME);
                }
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

    // --- Interrogation du serveur LMS ---
    if (now - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = now;

        static unsigned long lastServerPoll = 0;
        if (now - lastServerPoll > 60000 || !serverStatus.valid) {
            lastServerPoll = now;
            serverStatus = lms.getServerStatus();
        }

        PlayerStatus newPlayer;
        bool found = lms.findPlayer(appCfg.lms_player, newPlayer);

        static int notFoundCount = 0;

        if (found && newPlayer.isPlaying) {
            notFoundCount = 0;

            // Si on était en horloge auto, retour à l'écran principal
            if (currentScreen == SCR_CLOCK) {
                enterScreen(SCR_MAIN);
            }

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

            playerStatus = newPlayer;
            drawPlayingScreen(fullRedrawNeeded);
            fullRedrawNeeded = false;

        } else if (found) {
            notFoundCount = 0;
            // Player trouvé mais pas en lecture (pause/stop) → passer en horloge
            playerStatus = newPlayer;
            if (currentScreen == SCR_MAIN) {
                lastSongId    = -1;
                lastIsPlaying = false;
                enterScreen(SCR_CLOCK);
            }
        } else {
            // found == false : erreur réseau ou player disparu
            if (++notFoundCount >= PLAYER_LOST_POLLS && currentScreen == SCR_MAIN) {
                // Player introuvable depuis trop longtemps (éteint ?) → horloge
                notFoundCount    = 0;
                lastSongId       = -1;
                lastIsPlaying    = false;
                playerStatus     = PlayerStatus{};   // reset : isPlaying=false, valid=false
                enterScreen(SCR_CLOCK);
            }
        }
    }

    // --- Fallback : SCR_MAIN sans lecture (LMS injoignable au démarrage) → horloge ---
    if (currentScreen == SCR_MAIN && !playerStatus.isPlaying) {
        enterScreen(SCR_CLOCK);
    }

    // --- Défilement du texte pendant la lecture ---
    if (currentScreen == SCR_MAIN && playerStatus.valid && playerStatus.isPlaying && !fullRedrawNeeded) {
        drawPlayingScreen(false);
        drawProgress(false);
    }

    delay(20);  // ~50 fps max
}
