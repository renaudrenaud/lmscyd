//  LMS CYD — Logitech Media Server → ESP32 Cheap Yellow Display (ESP32-2432S028)

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
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
static bool  fullRedrawNeeded = true;

static unsigned long lastPoll    = 0;
static unsigned long lastClock   = 0;

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
    if (!forceRedraw && playerStatus.volume == lastVolume &&
        playerStatus.isPlaying == lastIsPlaying) return;

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

    // Barre de volume (droite)
    int volX = 230, volY = 7, volW = 60, volH = 10;
    drawBar(volX, volY, volW, volH, playerStatus.volume / 100.0f, C_VOLUME, C_TRACK_BG);
    display.setFont(&fonts::Font0);
    display.setTextColor(C_FORMAT, C_HEADER_BG);
    char vbuf[5];
    snprintf(vbuf, sizeof(vbuf), "%3d%%", playerStatus.volume);
    display.drawString(vbuf, 234, 18);

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
//  Écran horloge / veille (aucun player en lecture)
// =============================================================================
static void drawIdleScreen() {
    display.fillScreen(C_BG);

    // Heure
    struct tm t;
    if (getLocalTime(&t)) {
        char timeBuf[9], dateBuf[12];
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
        strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &t);

        display.setFont(&fonts::FreeSans24pt7b);
        display.setTextColor(C_CLOCK, C_BG);
        display.setTextDatum(lgfx::top_center);
        display.drawString(timeBuf, SCREEN_W / 2, 30);

        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(C_FORMAT, C_BG);
        display.drawString(dateBuf, SCREEN_W / 2, 100);
        display.setTextDatum(lgfx::top_left);
    }

    // Infos LMS
    display.setFont(&fonts::Font2);
    display.setTextDatum(lgfx::top_left);
    if (serverStatus.valid) {
        char buf[48];
        display.setTextColor(C_FORMAT, C_BG);
        snprintf(buf, sizeof(buf), "LMS v%s", serverStatus.version.c_str());
        display.drawString(buf, MARGIN, 135);
        snprintf(buf, sizeof(buf), "Players: %d  Albums: %d  Songs: %d",
                 serverStatus.playerCount, serverStatus.totalAlbums, serverStatus.totalSongs);
        display.drawString(buf, MARGIN, 155);
    } else {
        display.setTextColor(0xFF4040u, C_BG);
        String lmsAddr = String("LMS unreachable — ") + appCfg.lms_ip;
        display.drawString(lmsAddr.c_str(), MARGIN, 135);
    }

    display.setTextColor(0x303030u, C_BG);
    display.setFont(&fonts::Font0);
    display.drawString("Touch left=prev  center=play/pause  right=next", MARGIN, 225);
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
//  Gestion du tactile
// =============================================================================
static unsigned long lastTouchMs = 0;
static const int     TOUCH_DEBOUNCE_MS = 400;

static void handleTouch() {
    int16_t tx, ty;
    if (!display.getTouch(&tx, &ty)) return;   // pas de toucher
    unsigned long now = millis();
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    lastTouchMs = now;

    if (!playerStatus.valid || playerStatus.playerid.isEmpty()) return;

    // Zones : gauche = précédent | centre = play/pause | droite = suivant
    if (tx < SCREEN_W / 3) {
        lms.prevTrack(playerStatus.playerid);
    } else if (tx < 2 * SCREEN_W / 3) {
        if (playerStatus.isPlaying)
            lms.pause(playerStatus.playerid);
        else
            lms.play(playerStatus.playerid);
    } else {
        lms.nextTrack(playerStatus.playerid);
    }

    lastPoll = 0;
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

    // --- Gestion du tactile ---
    handleTouch();

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

        if (found && newPlayer.isPlaying) {
            bool songChanged = (newPlayer.currentSongId != lastSongId);

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
            // Player trouvé mais pas en lecture (pause/stop) → écran horloge
            playerStatus = newPlayer;
            if (lastIsPlaying || now - lastClock > 1000) {
                lastClock        = now;
                lastIsPlaying    = false;
                fullRedrawNeeded = true;
                lastSongId       = -1;
                drawIdleScreen();
            }
        }
        // found == false : erreur réseau transitoire → on garde l'écran courant
    }

    // --- Défilement du texte pendant la lecture ---
    if (playerStatus.valid && playerStatus.isPlaying && !fullRedrawNeeded) {
        drawPlayingScreen(false);
        drawProgress(false);
    }

    // --- Horloge de veille (mise à jour chaque seconde) ---
    if (!playerStatus.isPlaying && now - lastClock > 1000) {
        lastClock = now;
        drawIdleScreen();
    }

    delay(20);  // ~50 fps max
}
