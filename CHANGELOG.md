# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Planned
- **Auto brightness** — adjust backlight level using the onboard photoresistor (GPIO 34/35)
- **Brightness in config** — set `display_brightness` from `config.json` instead of recompiling
- **Smooth track transition** — quick black fade between tracks instead of hard redraw
- **Volume swipe** — vertical swipe gesture on the touch screen to raise/lower volume
- **Seek on progress bar** — tap the progress bar to jump to a position in the track
- **Touch visual feedback** — brief highlight when a touch zone is activated
- **Interpolated elapsed time** — increment the progress bar locally between LMS polls for smoother animation
- **Player selection screen** — if `lms_player` is empty, show available players at startup and let the user pick one by touch
- **Player name on idle screen** — display the followed player's name on the clock screen

---

## [1.1.0] - 2026-03-01

### Added
- **Home menu** — long-press anywhere on the screen (1.5 s) to open the navigation menu; short-tap to select an item; "Now Playing" returns to the main screen
- **LMS Server Info screen** — shows configured IP / port, LMS version, connected player count, album and song counts
- **Players Info screen** — lists all known players with IP address, MAC address and firmware version; connected players highlighted in green
- **Web Portal** — starts an open Wi-Fi AP (`LMS-CYD-Config`); captive-portal DNS redirects the phone browser automatically to `192.168.4.1`; HTML form lets you edit all settings (WiFi, LMS IP/port, player, timezone) and reboot; long-press to cancel
- **`saveAppConfig()`** — writes updated settings back to `config.json` on LittleFS; used by the Web Portal after form submission

---

## [1.0.0] - 2026-03-01

### Added
- Album art display: 120×120 JPEG fetched from LMS, heap-cached per track
- Two-column playing screen: album art left, artist / album / title right
- Smooth text scrolling for long artist, album and title strings
- Progress bar with elapsed time, total duration and track number (e.g. 3/13)
- Audio format info bar: codec, sample rate, bit depth, bitrate
- Touch controls: left = previous, center = play/pause, right = next
- Idle clock screen with large digital clock and date (NTP-synced)
- IANA timezone support via `config.json` (e.g. `"Asia/Shanghai"`)
- LMS server stats on idle screen: player count, album count, song count
- Sleep timer indicator in the header
- Config loaded from LittleFS (`data/config.json`) — no recompile needed
- WiFi auto-reconnect with resilience to transient LMS HTTP errors
- Version, project name and GitHub URL displayed on boot and connecting screens
- Version defined in a single `src/version.h` file
