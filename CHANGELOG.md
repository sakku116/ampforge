# Changelog

All notable changes to Amp Forge are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [0.2.1] — 2026-06-13

### Fixed
- Section bypass state not saved — bypassing a section, saving a preset or template, then reloading the app reset all sections to active.
- On/off badge in horizontal chain view not responding — clicking the bypass badge selected the row instead of toggling bypass after the row-highlight fix.
- Bypass and remove operations broken after switching templates — during the 25 ms audio crossfade, edit operations targeted the old chain instead of the newly loaded one, causing bypass to silently fail and remove to sometimes wipe the entire chain.
- Bypass badge response delay removed — the toggle now fires on mouse press instead of mouse release, matching the feel of a physical hardware bypass switch.

---

## [0.2.0] — 2026-06-12

### Added
- Badge box (left of plugin name) is now the bypass / activate toggle — no separate button needed. Shows the bound key or CC label when a control is assigned.
- `LevelMeterBar` compact mode — master input/output meters now render as a thin 8 px strip aligned below each slider.

### Changed
- Active stomp slot badge color changed from amber to light blue so it is no longer confused with the bypass (amber) state.
- Preset section slot rows now use the same background as stomp rows — section type is distinguished by the header badge, not row color.
- Peak meter in the section header is hidden in horizontal chain view (vertical view only).
- Footer panel compacted: row height 34 → 28 px, total footer height 175 → 128 px. Meter strip merged into the master area.
- In Gain and Out Vol reset buttons removed; double-click either slider to reset to unity.
- Thin separator lines added between footer sections (Templates / Master / Control) for visual clarity.
- User Guide rewritten in English with expanded Stomp vs Preset section explanations and examples.

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
