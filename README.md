# LMS CYD

**Logitech Media Server display for the ESP32 Cheap Yellow Display**

A full-featured now-playing screen for [Logitech Media Server](https://lyrion.org/)
running on the [ESP32-2432S028](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
(320×240 ILI9341 TFT + XPT2046 touch), built with
[LovyanGFX](https://github.com/lovyan03/LovyanGFX) and
[ArduinoJson](https://arduinojson.org/).

---

## Features

- **Album art** — 120×120 JPEG fetched directly from LMS, cached in heap
- **Now playing** — artist, album and title with smooth pixel scrolling
- **Progress bar** — elapsed / duration with track number (e.g. 3/13)
- **Audio format** — codec, sample rate, bit depth, bitrate
- **Touch controls** — tap left / center / right for prev / play-pause / next
- **Idle clock** — large NTP-synced digital clock when nothing is playing
- **IANA timezone** — set your timezone by name in `config.json`
- **Zero-recompile config** — all network settings in `data/config.json`
- **Resilient** — survives transient WiFi drops and LMS timeouts
- **Navigation menu** — long-press the screen to open the menu
- **LMS Server Info** — IP, port, LMS version, library statistics
- **Players Info** — per-player IP, MAC address and firmware version
- **Web Portal** — edit all settings from any browser, no USB needed

## Screen layout

![screen](/resources/player_screen_01.jpg)

```
┌──────────────────────────────────┐
│  ▶  Player name          75%vol  │  header
├────────────┬─────────────────────┤
│            │  Artist             │
│  Album art │  Album              │
│  120×120   │  Title              │
├────────────┴─────────────────────┤
│  01:23 / 04:56            3/13   │  progress
│  ████████████░░░░░░░░░░░░░░░░░░  │
├──────────────────────────────────┤
│  FLAC  44.1kHz  16bit  1411k     │  format
├──────────────────────────────────┤
│  |<< PREV    PLAY/PAUSE  NEXT >>|│  controls
└──────────────────────────────────┘
```

## Hardware

| Part | Details |
|------|---------|
| Board | ESP32-2432S028 ("Cheap Yellow Display") |
| Display | ILI9341 — 320×240 TFT, HSPI |
| Touch | XPT2046, VSPI |
| Backlight | GPIO 21, PWM |

## Dependencies

Managed automatically by PlatformIO:

```ini
lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
    bblanchon/ArduinoJson @ ^7.3.1
```

## Installation

### 1 — Clone and open

```bash
git clone https://github.com/renaudrenaud/lmscyd.git
cd lmscyd
```

Open the project in VS Code with the PlatformIO extension, or use the CLI.

### 2 — Configure

Edit `data/config.json`:

```json
{
  "wifi_ssid":     "YourNetwork",
  "wifi_password": "YourPassword",
  "lms_ip":        "192.168.1.100",
  "lms_port":      9000,
  "lms_player":    "",
  "timezone":      "Europe/Paris"
}
```

| Field | Description |
|-------|-------------|
| `wifi_ssid` | Your WiFi network name |
| `wifi_password` | Your WiFi password |
| `lms_ip` | IP address of your LMS server |
| `lms_port` | LMS port (default `9000`) |
| `lms_player` | Player name to follow, or `""` for the first active player |
| `timezone` | IANA timezone name (see list below) |

### 3 — Flash

```bash
# Flash the filesystem (config.json) — only needed on first install or config change
pio run -t uploadfs

# Flash the firmware
pio run -t upload
```

## Supported timezones

A selection of common IANA timezone names supported out of the box:

`UTC` · `Europe/London` · `Europe/Paris` · `Europe/Berlin` · `Europe/Moscow` ·
`America/New_York` · `America/Chicago` · `America/Los_Angeles` · `America/Sao_Paulo` ·
`Asia/Dubai` · `Asia/Kolkata` · `Asia/Shanghai` · `Asia/Tokyo` · `Asia/Seoul` ·
`Australia/Sydney` · `Pacific/Auckland`

Full list in `src/main.cpp` → `ianaToposix()`.

## Touch zones

| Gesture | Action |
|---------|--------|
| Short tap — left third | Previous track |
| Short tap — center third | Play / Pause |
| Short tap — right third | Next track |
| **Long press (1.5 s)** | **Open navigation menu** |

## Navigation menu

Hold the screen for 1.5 seconds from any screen to open the menu.

| Menu item | Description |
|-----------|-------------|
| Now Playing | Return to the play / idle screen |
| LMS Server Info | IP, port, LMS version, album and song counts |
| Players Info | IP, MAC address and firmware for every known player |
| Web Portal | Edit settings from a phone browser — no USB needed |

## Web Portal

1. Select **Web Portal** from the menu.
2. The device creates an open Wi-Fi network: **`LMS-CYD-Config`**
3. Connect your phone — a captive-portal popup appears automatically.
4. Fill in the form and tap **Save & Reboot**.
5. To cancel without saving, long-press the screen.

> The portal edits `config.json` on the device and reboots immediately after saving.

## Building from source

```bash
pio run            # compile only
pio run -t upload  # compile + flash
pio device monitor # serial output at 115200 baud
```

## License

MIT — see [LICENSE](LICENSE) if present, otherwise do whatever you want with it.
