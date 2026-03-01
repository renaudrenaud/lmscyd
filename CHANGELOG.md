# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
