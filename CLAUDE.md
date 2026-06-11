# Amp Forge — AI Session Context

Quick-load context for Claude Code sessions. Read this before touching any file.

---

## Project Summary

**What it is:** A real-time desktop VST3/VST2 plugin host for guitar processing. Windows only, built with JUCE 8.0.2 + C++20. Two executables: the GUI host and a subprocess scanner.

**Build:**
```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug --parallel
# or: make build / make release
```
Output: `build/AmpForge_artefacts/Debug/Amp Forge.exe` + `AmpForgeScanWorker.exe` (auto-copied alongside host).

**Optional SDK auto-detection:**
- ASIO: place SDK at `asio/` in project root → `JUCE_ASIO=1`
- VST2: place SDK at `vst2sdk/` in project root → `JUCE_PLUGINHOST_VST=1`

**Config/data location:** `%APPDATA%\AmpForge\` — log (`host.log`), presets (`presets/*.tfpreset`), app settings (audio device, templates, control map, scan paths), plugin scan cache (`pluginCache.xml`).

**Plugin scan cache:** `PluginScanner` persists `KnownPluginList` + a per-file modtime map to `pluginCache.xml`. Startup uses `scanAll(false)` (incremental — only spawns a scan subprocess for new/changed files; deleted plugins pruned). The "Rescan" button uses `scanAll(true)` (clear + full rescan). Makes startup near-instant after the first scan.

---

## Architecture

### Two Executables

| Target | File | Role |
|--------|------|------|
| `AmpForge` | `src/main.cpp` | GUI host — all UI + audio |
| `AmpForgeScanWorker` | `src/scan_worker_main.cpp` | Console subprocess — loads one plugin DLL, prints XML to stdout, exits. Host parses and merges. Prevents crashed plugin from killing host. |

### Source Files

| File | Role |
|------|------|
| `MainComponent.h/.cpp` | Root UI component. Owns everything. Handles button callbacks, timer, key press, persistence, MIDI learn orchestration. |
| `AudioEngine.h/.cpp` | Wraps `AudioDeviceManager`. Real-time I/O callback. Master input gain / output volume / mute (atomic floats). CPU metrics. MIDI input routing via callback to MainComponent. |
| `PluginHost.h/.cpp` | Façade over `PluginChain`. Owns `AudioPluginFormatManager`. Manages plugin load/unload, editor windows, chain rebuild and crossfade. |
| `PluginChain.h/.cpp` | Core: ordered slot list. Lock-free atomic publish model (immutable `SlotList` snapshot). Supports sections (Stomp/Preset), slot-level bypass, section-level bypass, slot identity (`slotId`). |
| `PluginScanner.h/.cpp` | Enumerates VST3/VST2 paths. Maintains `KnownPluginList`. Merges custom user paths with defaults. |
| `ScanSubprocess.h/.cpp` | Spawns scan worker per file. Parses XML output. |
| `PluginScanGuard.h/.cpp` | Windows SEH wrapper for plugin DLL load. Returns `addedTypes / noTypes / crashed`. |
| `Preset.h/.cpp` | ValueTree ↔ XML serialization of chain snapshots (`SlotSpec[]` + `SectionDef[]`). File I/O. Version 1→2 migration (v1: no sections). |
| `TemplateManager.h/.cpp` | Ordered list of named templates (each = chain snapshot). Serializes to ValueTree. |
| `ControlMap.h/.cpp` | Stores trigger→action bindings and expression mappings. Pure data; owner executes. |
| `ChainListBox.h/.cpp` | `ListBoxModel` for signal chain. Rows = `SectionHeaderComponent` or `ChainSlotComponent`, both 46px uniform height. All slot/section callbacks wired through `ChainListModel`. |
| `ToneForgeLookAndFeel.h/.cpp` | Dark "stage" theme. `tf::colour::` namespace: `background`, `surface`, `surface2`, `surface3`, `outline`, `accent` (teal), `accentDim`, `danger`, `warn` (amber), `text`, `textDim`. |
| `HostDebug.h` | `HostDebug::log()` — wraps `juce::Logger` with `[AmpForge]` prefix. |

---

## Threading Model

`PluginChain` uses an atomic shared_ptr publish pattern:
- **Message thread** — all structural edits (add/remove/move slot, section changes). Builds a new `shared_ptr<SlotList>` and atomically stores it in `activeList`.
- **Audio thread** — reads `activeList.load()` at start of each block. Never blocks, never deletes.
- **Crossfade** — `fadeInList` holds incoming chain; audio thread blends over N samples, then promotes to `activeList`.
- **Bypass** — per-slot `atomic<bool>`, no republish needed.

---

## Key Data Structures

### `PluginChain::SectionDef`
```cpp
struct SectionDef {
    enum class Type { stomp, preset };
    int          id;      // stable, assigned by nextSectionId++
    juce::String name;
    Type         type;
    bool         bypassed;  // section-level mute
};
```

### `PluginChain::SlotInfo` (query result, read-only snapshot)
```cpp
struct SlotInfo {
    juce::String name;          // customName ?? plugin name
    juce::String originalName;
    juce::String format;
    bool bypassed;
    bool hasCustomName;
    int  sectionId;
    bool isPreset;
    int  slotId;    // STABLE — never changes on reorder/move
};
```

### `PluginChain::SlotSpec` (serialization / rebuild input)
```cpp
struct SlotSpec {
    juce::PluginDescription description;
    juce::MemoryBlock state;
    bool bypassed;
    juce::String customName;
    int sectionId;
    int slotId;     // 0 = assign new on load; >0 = restore exact
};
```

### `ControlAction`
```cpp
struct ControlAction {
    enum class Type { none, nextTemplate, prevTemplate, loadTemplate,
                      toggleBypass, activatePresetSlot };
    Type type  = Type::none;
    int  index = 0;   // template index (loadTemplate) OR stable slotId (toggleBypass/activatePresetSlot)
};
```
**IMPORTANT:** For `toggleBypass` and `activatePresetSlot`, `index` stores `slotId` (not positional index). Resolve with `findSlotIndexById(action.index)` at execution time.

### `ControlTrigger`
```cpp
struct ControlTrigger {
    enum class Type { none, midiNote, midiCC, midiProgram, key };
    Type type   = Type::none;
    int  number = 0;   // note/CC/program number, or juce::KeyPress key code
};
```

---

## Implemented Features (complete)

| Feature | Key files |
|---------|-----------|
| Real-time audio I/O | `AudioEngine` |
| VST3/VST2 plugin scan (safe subprocess) | `PluginScanner`, `ScanSubprocess`, `PluginScanGuard` |
| Custom plugin scan paths (persistent) | `MainComponent::openScanPaths()`, key `pluginScanPaths` |
| Signal chain — add/remove/reorder plugins | `PluginChain::addPlugin/removePlugin/movePlugin` |
| Signal chain — section Stomp / Preset | `PluginChain::SectionDef`, `ChainListBox` section headers |
| Section bypass (mutes all slots in section) | `PluginChain::setSectionBypassed`, `SectionDef::bypassed` |
| DnD slot reordering across sections | `ChainListBox` drag-drop → `moveSlotAt` |
| Slot bypass toggle | `PluginChain::setBypass` |
| Preset section — exclusive slot activation | `PluginChain::activatePresetSlot` |
| Plugin editor windows | `PluginHost::openEditor` |
| Master gain / volume / mute | `AudioEngine` atomic floats, sliders in MainComponent footer |
| Preset save/load (.tfpreset XML) | `Preset::saveToFile/loadFromFile` |
| Templates (named chain snapshots) | `TemplateManager`, `captureTemplate/recallTemplate` |
| Template dirty tracking | `templateDirty` flag + "●" label |
| MIDI/key control mapping (learn mode) | `ControlMap`, `armActionLearn`, `bindingLearnComplete` |
| Control binding hint labels on chain rows | `ChainRow::controlHint`, `refreshChainList` |
| Expression pedal CC → parameter mapping | `ControlMap::ExpressionMapping`, `armExpressionLearn` |
| Stable slotId (bindings survive reorder) | `PluginChain::Slot::slotId`, `findSlotIndexById` |
| Right-click "Learn Control" on slot rows | `ChainListModel::onLearnControl` context menu |
| Key label display (char not code) | `juce::KeyPress(number).getTextDescription()` |
| Per-slot post-effect volume control | `PluginChain::setSlotGain`, `Slot::postGain` atomic, `VolumeControl` button on each slot row |
| Per-section output volume control | `PluginChain::setSectionGain`, `SectionDef::gain`, `Slot::sectionOutputGain` atomic (applied at last slot of section) |
| Section level metering | `Slot::peakLevel` atomic, `SectionHeaderComponent` timer 24fps, horizontal bar with peak-hold |
| Volume control UI | `VolumeControl` — button (speaker icon + dB label); click opens `CallOutBox` with horizontal slider + reset button |
| Horizontal chain view (toggle) | `ChainHorizontalView`, `SectionColumnComponent`; ↔/↕ toggle button; persisted as `chainViewMode` |
| Duplicate plugin | Right-click → Duplicate on slot row; `PluginChain::duplicatePlugin`, `PluginHost::duplicatePlugin`, `ChainListModel::onDuplicate`, `MainComponent::duplicateSlotAt` |
| Slot right-click context menu | Open Editor, Duplicate, Learn Control, Rename, Reset Name, Remove — all via right-click only (no dedicated row buttons for editor/remove) |
| Section remove confirmation | Right-click → Remove Section triggers `juce::AlertWindow` confirmation before `removeSection`; no ✕ button on section header |
| Control binding badge | Square badge left of plugin name (side = rowHeight−4 ≈ 32px); shows assigned CC/note/key label; amber fill for BYP bindings, teal for ACT; dimmed border+text when slot bypassed; empty outline when unassigned |
| Preset row tint | Preset section slot rows have amber overlay (`warn` @ 10% alpha); selected preset row uses amber highlight |

---

## Important Invariants

1. **`slotId` is stable.** Assigned at slot creation (`nextSlotId++`), never changes through `movePlugin`, `moveSectionUp/Down`, or save/load roundtrip. `ControlAction::index` stores `slotId` for bypass/preset actions — always resolve via `findSlotIndexById()`, never use positional index directly.

2. **Section order in `sectionDefs`.** `moveSectionUp/Down` iterates `sectionDefs` in new order when rebuilding `SlotList`. Old bindings (positional index) break — this was the bug slotId fixes. Don't revert to positional index.

3. **`buildList()` restores slotId.** If `spec.slotId > 0`, restore it and update `nextSlotId = max(nextSlotId, spec.slotId + 1)`. If `spec.slotId == 0` (old file), assign new.

4. **Preset section exclusive activation.** `activatePresetSlot(index)` bypasses all other slots in the same section atomically. Stomp slots toggle independently.

5. **`ControlTrigger::toString()`** must use `juce::KeyPress(number).getTextDescription()` for key triggers — not `juce::String(number)` which gives raw key code.

6. **`refreshChainList()`** must be called after any action that changes visible chain state (clear maps, bypass, move, etc.) to update hint labels and row rendering.

---

## Persistence Keys (ApplicationProperties)

| Key | Content |
|-----|---------|
| `audioDeviceState` | XML from `AudioDeviceManager::createStateXml()` |
| `lastPresetPath` | Full path of last saved/loaded `.tfpreset` |
| `scenes` | Templates state (key kept as "scenes" for backward compat with old saves) |
| `controlMap` | Control bindings + expression mappings |
| `pluginScanPaths` | Semicolon-separated list of custom scan directories |
| `chainViewMode` | `bool` — `true` = horizontal layout, `false` = vertical (default) |

---

## Preset File Format (v2)

```xml
<TONEFORGE_PRESET name="My Preset" version="2">
  <SECTION sectionId="1" sectionName="Stomp 1" sectionType="stomp" sectionGain="0.891"/>
  <SECTION sectionId="2" sectionName="Presets" sectionType="preset"/>
  <SLOT bypassed="0" sectionId="1" slotId="3" customName="" postGain="1.122" state="<base64>">
    <PLUGIN ... />   <!-- juce::PluginDescription XML -->
  </SLOT>
  ...
</TONEFORGE_PRESET>
```
v1 files (no SECTION nodes) migrate to a synthetic "Stomp 1" section. `slotId` absent → `buildList` assigns fresh IDs (existing control bindings need re-learn, one-time migration cost). `postGain`/`sectionGain` absent → defaults to 1.0 (backward compatible).

---

## UI Layout (MainComponent)

```
┌─────────────────────────────────────────────────────────┐
│ HEADER: title / metrics / Audio Settings / Rescan / Scan Paths │
├─────────────────────────────────┬───────────────────────┤
│ LIBRARY (left panel)            │ SIGNAL CHAIN (right)  │
│  - Plugin palette (ListBox)     │  - ChainListBoxView   │
│  - + Add to Chain               │    (section headers + │
│                                 │     slot rows)        │
│                                 │  - + Stomp / + Preset │
├─────────────────────────────────┴───────────────────────┤
│ FOOTER rows (top → bottom):                             │
│  1. Save/Load preset                                    │
│  2. TEMPLATES: [label][dropdown][◀][▶][⊕][↑][✎][✕][●] │
│  3. MASTER: [In Gain slider] [Out Vol slider] [MUTE]   │
│  4. CONTROL: [status label] [Learn Expression] [Clear] │
└─────────────────────────────────────────────────────────┘
```

---

## Git / Workflow Notes

- User pushes manually — never `git push` without explicit ask.
- Ask before deleting files or branches.
- Local git only; no CI/CD configured.
- Current branch: `main`.

---

## Recently Completed Work (last 3 sessions)

1. **Horizontal chain view:** `ChainHorizontalView` + `SectionColumnComponent` in `ChainListBox.h/.cpp`. Toggle button (↔/↕) switches vertical ↔ horizontal mode. Sections rendered as side-by-side columns (240px wide, 8px gap) in a `juce::Viewport` with horizontal scrollbar. Section headers show ◀▶ instead of ▲▼. Slot DnD works cross-column. Persisted as `chainViewMode`.
2. **Signal chain UX polish:** Duplicate plugin via right-click (`PluginChain::duplicatePlugin`). Open Editor + Remove moved to right-click menu only — no dedicated row buttons. Section ✕ button removed; Remove Section via right-click shows `juce::AlertWindow` confirmation. Shared layout constants (`kBtnW=28`, `kVolW=30`, `kBtnGap=4`, `kRightMargin=4`) align buttons across section headers and slot rows.
3. **Visual refinements:** Control binding badge — square (side ≈ rowHeight−4), 11pt bold font, amber/teal fill by action type, dimmed when slot bypassed. Preset slot rows have amber tint (`warn` @ 10% alpha). Uniform row height 46px (was 52px). Library and Signal Chain panel header rows vertically aligned.
