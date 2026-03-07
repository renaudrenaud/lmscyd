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
- **Touch visual feedback** — brief highlight when a touch zone is activated
- **Interpolated elapsed time** — increment the progress bar locally between LMS polls for smoother animation
- **Player selection screen** — if `lms_player` is empty, show available players at startup and let the user pick one by touch
- **Player name on idle screen** — display the followed player's name on the clock screen

---

## [1.3.1] - 2026-03-07

### Fixed
- **Album art missing on some tracks** — LMS can return cover art as PNG instead of JPEG depending on the source file; `drawCover` now detects the format from the magic bytes (`FF D8` = JPEG, `89 50 4E 47` = PNG) and calls `drawJpg` or `drawPng` accordingly
- **Cover buffer cap and chunked transfers** — increased buffer cap from 30 KB to 64 KB; added dynamic `realloc` growth for chunked HTTP responses to avoid silent truncation
- **Partial download stored as valid cover** — if an error occurs mid-download, the partial buffer is now discarded instead of being passed to `drawJpg`
- **Retry blocked after failed download** — fixed early-return guard so a failed cover fetch is retried on the next poll cycle

---

## [1.3.0] - 2026-03-03

### Added
- **Dual timezone display** — optional `timezone2` field in `config.json`; when set, both clocks are shown simultaneously on all clock screens (digital: large primary + smaller secondary with date; analog: primary clock face + secondary time below date); city name extracted from IANA string (e.g. `"Europe/Paris"` → `"Paris"`); configurable via Web Portal

---

## [1.2.3] - 2026-03-03

### Added
- **WiFi signal icon** — 4-bar icon in the top-right of the header; color goes from red (0 bars, < −85 dBm) to green (4 bars, ≥ −55 dBm); redrawn only when the bar level changes

---

## [1.2.2] - 2026-03-03

### Fixed
- **Long-press reliability** — touch handler rewritten as a 3-state machine (`IDLE` / `PRESSING` / `PENDING_TAP`); XPT2046 glitches (brief contact drops < 150 ms) no longer reset the long-press timer; threshold reduced from 1500 ms to 800 ms; short-tap confirmation delayed by `TOUCH_GLITCH_MS` to avoid spurious actions during a hold

---

## [1.2.1] - 2026-03-03

### Changed
- **Auto-switch to clock screen** — when no player is playing (pause or stop), the display automatically switches to `SCR_CLOCK`; playback resuming switches back to the main screen automatically

### Fixed
- **Player turned off / lost** — after `PLAYER_LOST_POLLS` (5) consecutive polls with no player found, the display switches to the clock screen instead of staying stuck on the last track info

---

## [1.2.0] - 2026-03-01

### Added
- **Analog clock** — new idle screen style with circle, 60 tick marks, thick hour and minute hands, thin second hand with counterweight; center dot in accent color
- **Clock style setting** — `clock_style` field in `config.json` (`"digital"` or `"analog"`); selectable from the Web Portal; default is `"digital"`
- **Clock screen** — dedicated screen accessible from the home menu; tap anywhere to cycle between clock styles; long-press to return to menu

### Fixed
- **Clock flickering** — incremental redraw replaces full `fillScreen` each second; analog clock erases only the hands and restores the face, digital clock overwrites digits in place; no more visible black flash

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
