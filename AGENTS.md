# Amp Forge — Agent Instructions

Authoritative project context and workflow for every coding agent. Read this before touching files.

## Conventions

- Write all codebase and git artifacts in English: code, comments, UI text, documentation, commits, branches, PRs, and reviews. Chat may use the user's language.
- Treat this file as the project-context source of truth. `CLAUDE.md` only points here.

## Agent skills

### Issue tracker

GitHub Issues via `gh`. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the default five labels. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `CONTEXT.md` and `docs/adr/`. See `docs/agents/domain.md`.

## Handoffs

Use the `handoff` skill when creating or resuming a handoff, and offer one after significant work. Store every generated handoff in `.handoffs/` as `YYYYMMDD_NN_title.md`: `NN` is the zero-padded daily sequence, starting at `01`; `title` is a concise kebab-case summary. On resume, inspect `.handoffs/` rather than a root `HANDOFF.md`.

## Project

Amp Forge is a Windows-only, C++20/JUCE 8.0.2 real-time guitar VST3/VST2 host. It has two executables:

| Target | Entry point | Responsibility |
|---|---|---|
| `AmpForge` | `src/main.cpp` | GUI, audio, and host |
| `AmpForgeScanWorker` | `src/scan_worker_main.cpp` | Isolated per-plugin scanner; emits XML to stdout so a crashing plugin cannot crash the host |

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug --parallel
# or: make build / make release
```

The host and copied scan worker are in `build/AmpForge_artefacts/Debug/`. Place optional SDKs at `asio/` (`JUCE_ASIO=1`) and `vst2sdk/` (`JUCE_PLUGINHOST_VST=1`).

Runtime data lives in `%APPDATA%\AmpForge\`: `host.log`, `presets/*.tfpreset`, settings, and `pluginCache.xml`. The scanner persists `KnownPluginList` plus file modification times: startup uses incremental `scanAll(false)` and Rescan uses full `scanAll(true)`.

## Source Map

| Area | Files | Responsibility |
|---|---|---|
| Root UI | `MainComponent.*` | Owns UI, callbacks, timer, persistence, MIDI learn, and chain refresh |
| Audio | `AudioEngine.*` | `AudioDeviceManager`, real-time I/O, atomic master gain/volume/mute, CPU metrics, MIDI routing |
| Hosting | `PluginHost.*`, `PluginChain.*` | Formats, instances, editors, chain snapshots, crossfade, sections, bypass, stable slot identity |
| Scanning | `PluginScanner.*`, `ScanSubprocess.*`, `PluginScanGuard.*` | Plugin discovery, cache, worker subprocess, Windows SEH load guard |
| Persistence | `Preset.*`, `TemplateManager.*`, `ControlMap.*` | Presets, named chain templates, trigger/action and expression mappings |
| Keyboard capture | `KeyboardControlController.*`, `KeyboardCaptureAdapter.*` | Session-global keyboard policy, Win32 hook translation, exclusive ownership, and message-thread action delivery |
| Chain UI | `ChainListBox.*` | Vertical rows and horizontal columns, section/slot interactions, levels, volume controls |
| Theme and logs | `ToneForgeLookAndFeel.*`, `HostDebug.h` | `tf::colour` stage palette and `[AmpForge]` logger |

## Chain Rules

`PluginChain` uses immutable `SlotList` snapshots published atomically:

- The message thread performs structural edits under its edit lock, builds a replacement list, then publishes it.
- The audio thread loads `activeList` once per block; it never blocks or deletes. Per-slot bypass and gains are atomics.
- A crossfade holds the incoming `fadeInList`, blends it, then promotes it to `activeList`.
- `activeList.load()` is the chain playing on the audio thread. Use it only for audio work plus `publishWithCrossfade` and `prepare`.
- `displayList()` is `fadeInList` when present, otherwise `activeList`; it is the chain the UI shows. `currentList()` delegates to it, so every message-thread edit targets the visible chain during a template-switch fade. `publish()` cancels a pending fade for an immediate edit.

### Stable identity and sections

- `slotId` is stable across reordering and persistence. Assign with `nextSlotId++`; restore a positive persisted ID and advance `nextSlotId`; assign a new ID for `0`.
- `ControlAction::index` holds a template index only for `loadTemplate`. For `toggleBypass` and `activatePresetSlot`, it holds `slotId`; resolve it with `findSlotIndexById()` at execution time.
- Rebuild in `sectionDefs` order. Section identity is stable; positional bindings are invalid after a reorder.
- Stomp slots bypass independently. Activating a preset slot atomically bypasses every other slot in its section.
- Section bypass is persisted and propagated atomically to its slots. Section output gain applies at the last slot; slot post-gain applies after its plugin.
- Refresh the visible chain with `refreshChainList()` after any chain or control-map change that affects rows, hints, or state.

## UI and Capabilities

- The library scans VST3/VST2 safely, supports persistent custom paths, and adds selected plugins to the chain.
- The chain supports Stomp and Preset sections; slot add, remove, duplicate, rename, reorder, drag across sections, section rename/reorder, confirmed section removal, bypass, plugin editors, and per-slot/section volume.
- Templates capture and recall chain snapshots with dirty tracking. Presets save and load `.tfpreset` files.
- MIDI notes, CC, programs, and keys can trigger template navigation/loading, slot bypass, or preset activation. Expression CC maps to parameters. Key labels use `juce::KeyPress(number).getTextDescription()`.
- Global Keys optionally captures mapped bare physical keys while Amp Forge is unfocused; it starts off each session, leaves unmapped and modified keys alone, and Ctrl+Shift+F11 disables it.
- Slot context menus provide editor, duplicate, learn control, rename/reset name, and remove. Slot badges are the bypass/activate control: process them in `mouseDown`, including inside the horizontal-view viewport.
- Badge states: active stomp light blue, bypassed stomp amber, active preset teal, inactive preset dim outline. Assigned controls appear on the badge. Preset rows use `surface2`; section headers distinguish preset sections with amber.
- Section peak meters appear only in vertical view. `LevelMeterBar` compact mode removes its label row for the footer meters. The footer has compact preset, template, master, and control rows; master sliders reset to 1.0 on double-click.

## Persistence

Application properties:

| Key | Value |
|---|---|
| `audioDeviceState` | `AudioDeviceManager::createStateXml()` XML |
| `lastPresetPath` | Last `.tfpreset` path |
| `scenes` | Template state; name retained for backward compatibility |
| `controlMap` | Control bindings and expression mappings |
| `pluginScanPaths` | Semicolon-separated custom directories |
| `chainViewMode` | `true` horizontal, `false` vertical |

Preset v2 is `<TONEFORGE_PRESET version="2">` containing `SECTION` and `SLOT` nodes. Sections persist ID, name, type, non-default gain, and bypass state. Slots persist plugin description/state, bypass, section ID, stable slot ID, custom name, and non-default post-gain. Version 1 has no sections: load it as a synthetic `Stomp 1`; missing IDs receive new IDs and missing gains/bypass state default to `1.0`/`false`.

## Layout

- Header: title, metrics, audio settings, Rescan, scan paths.
- Main: plugin library and Add button on the left; chain view and add-section controls on the right.
- Footer, top to bottom: preset save/load; templates; master input/output/mute and meters; control status, expression learn, clear mappings.

## Git Workflow

- The user performs pushes; run `git push` only when explicitly asked. Ask before deleting files or branches.
- `main` is protected. Work reaches it only through a PR; never push to it directly.
- Cut `feature/*` or `fix/*` from `dev`, open a PR into `dev`, then open `dev` into `main` for releases.

```bash
git checkout dev && git pull
git checkout -b feature/my-feature
# work
git push -u origin feature/my-feature
gh pr create  # base: dev
```

## Releases

Execute this workflow when asked to release `vX.Y.Z`.

1. Determine semantic version: breaking = major, compatible feature = minor, fix/docs = patch. `project(AmpForge VERSION x.y.z)` in `CMakeLists.txt` is the only version source.
2. On updated `dev`, change that version, update the README's latest-release link, and prepend `CHANGELOG.md` with user-visible commits since the prior tag (`git log vPREV..HEAD --oneline --no-merges`). Use present-tense bullets under `Added`, `Fixed`, or `Changed`; omit refactors and CI noise. Create the file with `# Changelog` if absent.
3. Commit `CMakeLists.txt`, `CHANGELOG.md`, and `README.md` as `chore: release vX.Y.Z`; push `dev`; create a `dev` → `main` PR titled `Release vX.Y.Z` with the changelog reference and a `make portable` checklist. Wait for user confirmation rather than merging it.
4. After the PR merges: update `main`, tag and push `vX.Y.Z`, merge `main` into `dev` and push `dev`, then run `make portable` (and optionally `make installer`). Upload `dist/AmpForge-X.Y.Z-portable-x64.zip` and, if built, the installer to the tagged GitHub Release.

```bash
git describe --tags --abbrev=0                 # latest tag
git log vPREV..HEAD --oneline --no-merges      # release commits
grep -m1 'VERSION [0-9]' CMakeLists.txt        # current version
make portable
```
