# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

---

## [1.6.2] - 2026-04-19

### Added
- **Fullscreen cover** (`SCR_COVER`) — tapping the album art on the Now Playing screen opens the cover full-screen (240×240 fetched from LMS, centered on black); tapping again returns to Now Playing

### Fixed
- **Casio clock** — brightened unlit LCD segments (`C_GOFF`) from `0xA4AE96` to `0xE5E7D9` for better readability

---

## [1.6.1] - 2026-04-19

### Added
- **Server Info auto-refresh** — screen polls LMS every 8 seconds but only redraws when player list actually changed (no flicker)
- **Player list sorted** — connected players shown first (white), disconnected below (grey)

### Changed
- **Clock screen** — date lines now include city name: `DD/MM/YYYY - Shanghai` / `DD/MM/YYYY - Paris`; secondary timezone uses same font size as primary
- **Default screen** — device boots directly to `SCR_CLOCK` instead of staying on splash screen

---

## [1.6.0] - 2026-04-19

### Added
- **Auto-discovery** — at startup, if the configured LMS IP is unreachable, the device scans the local /24 subnet on the LMS port. The first host that answers and returns a valid LMS response is adopted; the new IP is saved to `config.json` transparently. No manual reconfiguration needed when the server's IP changes.
- **Sidebar navigation** — persistent left-side bar with 5 drawn icons (Now Playing, Clock, Server, Players, Config); touch zones corrected for the board's inverted X axis.
- **Now-playing line on clock screen** — when a player is active, the dual-timezone clock screen shows player name and current album on a dedicated line.

### Fixed
- **Secondary timezone DST** — replaced manual POSIX offset parser (which ignored DST and had a sign bug) with `setenv/tzset/localtime_r`; Paris now correctly shows UTC+2 in summer.
- **Clock screen pollution** — `drawPlayingScreen()` was being called while on `SCR_CLOCK`, overlaying LMS data on top of the clock; now guarded to `SCR_MAIN` only.

### Planned
- **Auto brightness** — adjust backlight level using the onboard photoresistor (GPIO 34/35)
- **Brightness in config** — set `display_brightness` from `config.json` instead of recompiling
- **Smooth track transition** — quick black fade between tracks instead of hard redraw
- **Volume swipe** — vertical swipe gesture on the touch screen to raise/lower volume
- **Touch visual feedback** — brief highlight when a touch zone is activated
- **Player selection screen** — if `lms_player` is empty, show available players at startup and let the user pick one by touch
- **Player name on idle screen** — display the followed player's name on the clock screen

---

## [1.5.1] - 2026-03-07

### Changed
