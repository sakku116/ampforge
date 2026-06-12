# Changelog

All notable changes to Amp Forge are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [0.1.0] — 2026-06-12

### Added
- Real-time VST3/VST2 plugin host with signal chain (Stomp + Preset sections)
- Safe plugin scanning via isolated subprocess (`AmpForgeScanWorker.exe`) — crashes don't kill the host
- Incremental plugin scan cache — near-instant startup after first scan
- Custom plugin scan paths (user-managed, persistent)
- Per-slot and per-section volume control with dB display
- Section-level output metering with peak-hold indicator
- MIDI and keyboard control mapping with learn mode
- Expression pedal CC → parameter mapping
- Stable slot identity (`slotId`) so bindings survive reorder
- Named templates (chain snapshots) with dirty tracking
- Preset save/load (`.tfpreset` XML, versioned format)
- Horizontal chain view (sections as side-by-side columns)
- Right-click context menu per slot (Open Editor, Duplicate, Learn Control, Rename, Remove)
- Control binding badge on slot rows (amber = bypass, teal = activate)
- Preset slot rows with amber tint + exclusive activation
- Master input gain / output volume / mute controls
- Window size and position persistence
- Portable mode — place `portable.txt` next to exe, all data redirects to `data/` subfolder
- ASIO driver support (optional — auto-detected if SDK present)
- VST2 plugin support (optional — auto-detected if SDK present)
- LTO enabled for Release builds (smaller binaries, faster DSP)
- Background plugin loading at startup (UI stays responsive)
