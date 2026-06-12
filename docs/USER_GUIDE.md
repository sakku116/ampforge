# Amp Forge — User Guide

Amp Forge is a real-time desktop VST3/VST2 plugin host for guitar processing. Windows only.

---

## Main Window

```
┌──────────────────────────────────────────────────────────────┐
│ [Audio Settings]  [Rescan Plugins]  [Scan Paths...]  CPU/DSP │
├──────────────────────────┬───────────────────────────────────┤
│  LIBRARY                 │  SIGNAL CHAIN          [Save][Load]│
│                          │  ┌─────────────────────────────┐  │
│  Distortion XY    VST3   │  │ ■ Stomp 1    [⊘][↑][↓][🔊] │  │
│  Reverb ABC       VST3   │  │  [■] Distortion XY    [🔊]  │  │
│  Chorus ZZZ       VST3   │  │  [□] Reverb ABC       [🔊]  │  │
│  ...                     │  ├─────────────────────────────┤  │
│                          │  │ ■ Presets    [⊘][↑][↓][🔊] │  │
│  [+ Add to Chain]        │  │  [□] Lead             [🔊]  │  │
│                          │  │  [■] Crunch           [🔊]  │  │
│                          │  │  [□] Clean            [🔊]  │  │
│                          │  └─────────────────────────────┘  │
│                          │  [+ Stomp]  [+ Preset]            │
├──────────────────────────┴───────────────────────────────────┤
│  TEMPLATES: [Dropdown ▼] [◀] [▶] [⊕] [↑] [✎] [✕]  ●        │
│  MASTER:  In Gain [━━━━━━] Out Vol [━━━━━━] [MUTE] [In Ch ▼]│
│           [▬▬▬▬▬▬▬▬▬▬] [▬▬▬▬▬▬▬▬▬▬]  ← peak meters         │
│  CONTROL: [status]  [Learn Expression]  [Clear Maps]         │
└──────────────────────────────────────────────────────────────┘
```
*`[■]` = active badge (blue for stomp, teal for active preset)*  
*`[□]` = inactive / bypassed badge*

---

## Library (Left Panel)

Lists all VST3/VST2 plugins detected on your system.

- **Rescan Plugins** — re-scans default and custom folders. Each plugin is scanned in an isolated subprocess so a faulty plugin cannot crash the host.
- **Scan Paths...** — add or remove custom scan folders (in addition to Windows defaults).
- **+ Add to Chain** — add the selected plugin to the signal chain, or double-click a plugin name.

Default scan folders:
- `C:\Program Files\Common Files\VST3`
- `C:\Program Files (x86)\Common Files\VST3`

---

## Signal Chain (Right Panel)

Audio flows top to bottom (or left to right in horizontal view):  
**Input → Slot 0 → Slot 1 → … → Output**

Plugins are grouped into **sections**. The section type controls how the bypass / activate badge on each slot behaves.

### Section Types

#### Stomp Section

Every plugin toggles bypass independently — just like stomping individual pedals on a physical pedalboard.

```
┌────────────┐  ┌────────────┐  ┌────────────┐
│ Compressor │  │ Overdrive  │  │   Reverb   │
│   [■ ON]   │  │  [□ BYP]   │  │   [■ ON]   │
└────────────┘  └────────────┘  └────────────┘
```

Clicking the Overdrive badge turns it on without touching the others. Every slot is fully independent.

#### Preset Section

Only **one** plugin is active at a time. Selecting a slot automatically deactivates every other slot in the same section. Ideal for amp simulators or multi-FX rigs where you want exactly one tone loaded at a time.

```
┌────────────┐  ┌────────────┐  ┌────────────┐
│ Amp Clean  │  │ Amp Crunch │  │  Amp Lead  │
│  [■ ACT]   │  │  [□ SEL]   │  │  [□ SEL]   │
└────────────┘  └────────────┘  └────────────┘
                      ↑
            click to switch here — Clean and Lead mute automatically
```

There is always exactly one active slot in a preset section; you cannot deactivate all of them simultaneously.

### Section Header

Each section has a header row with control buttons:

| Button | Function |
|--------|----------|
| `⊘` | Bypass the entire section — mutes all plugins in it at once |
| `↑` / `↓` | Move this section up or down in the chain |
| `🔊` | Section output volume |

**Right-click** a section header to **Rename** or **Remove** it (removal asks for confirmation).

### Plugin Slot Row

| Element | Function |
|---------|----------|
| **Badge box** (left of name) | **Press** to toggle bypass (Stomp) or activate the slot (Preset) — fires on mouse-down for instant response. Blue = active stomp, teal = active preset, amber/dim = bypassed. Shows the bound key or CC label when a control is assigned |
| Plugin name | Display name and format (VST3 / VST2) |
| `🔊` Volume knob | Per-slot post-gain |
| Drag grip | Drag to reorder or move the slot into a different section |

**Right-click** a plugin row for more options:

| Option | Function |
|--------|----------|
| **Open Editor** | Open the plugin's own UI window |
| **Duplicate** | Clone this slot (copies plugin state) |
| **Learn Control** | Bind a keyboard key or MIDI event to this slot |
| **Rename** / **Reset Name** | Set or clear a custom display name |
| **Remove** | Delete the plugin from the chain |

### Moving Slots Across Sections

Drag a slot from one section and drop it into another. The slot automatically takes on the behavior of its destination section — a plugin dropped into a Preset section joins that section's exclusive group.

### Adding and Removing Sections

- **+ Stomp** — add a new Stomp section at the bottom of the chain
- **+ Preset** — add a new Preset section at the bottom of the chain

---

## Master Controls

Located in the footer.

| Control | Function |
|---------|----------|
| **In Gain** slider | Input gain before the plugin chain (0–200%). Double-click to reset to unity |
| **Out Vol** slider | Output volume after the plugin chain (0–200%). Double-click to reset to unity |
| **MUTE** | Silence the output (signal still flows through the chain) |
| **In Ch** | Select the audio input channel |
| Peak meters | Thin level bars below each slider showing real-time signal level |

---

## Preset — Save & Load

Saves and loads a complete snapshot of the signal chain to a `.tfpreset` file.

- **Save** — save the current chain (choose a file location)
- **Load** — load an existing `.tfpreset` file

A preset stores all sections and their order, every plugin, plugin state, individual slot bypass status, section bypass status, and control bindings. Bindings survive section reordering because they track stable slot IDs, not positional indexes.

Default preset folder: `%APPDATA%\AmpForge\presets\`

---

## Templates

A template is a named snapshot of the signal chain stored inside the app — no file dialog is needed when recalling. Useful for quick-switching between setups during a session.

| Control | Function |
|---------|----------|
| Dropdown | Select the active template |
| `◀` / `▶` | Step to the previous / next template |
| `⊕` | Save the current chain as a new template |
| `↑` | Update the active template with the current chain |
| `✎` | Rename the active template |
| `✕` | Delete the active template |
| `●` indicator | The current chain differs from the stored template |

---

## Control Mapping

Bind keyboard keys or MIDI (note / CC / program change) to specific slot actions.

### Learning a Binding

1. **Right-click** a plugin row in the signal chain
2. Select **Learn Control**
3. The CONTROL status bar shows *"Waiting for input…"*
4. Press a keyboard key, stomp a MIDI footswitch, or move a MIDI CC
5. The binding saves automatically

The bound action depends on the section type:
- **Stomp section** → toggle bypass for that slot
- **Preset section** → exclusively activate that slot

After learning, a small label (e.g. `Key Q`, `CC 64`) appears on the plugin row's badge.

Bindings use stable slot IDs — reordering sections or moving plugins does **not** break existing bindings.

### Expression Pedal

Click **Learn Expression** → select a slot and a parameter in the plugin editor → move the MIDI CC pedal → the mapping saves. The pedal then controls that parameter continuously over its full range.

### Clearing All Bindings

Click **Clear Maps** to delete every binding at once. All badge labels disappear immediately.

---

## Audio Settings

Click **Audio Settings** in the header to:
- Choose an audio driver (WASAPI / DirectSound / ASIO if the SDK is present)
- Select input and output devices
- Set sample rate and buffer size

Audio device state is saved automatically on exit.

---

## Troubleshooting

| Symptom | Solution |
|---------|---------|
| Plugin not appearing in the library | Click **Rescan Plugins**; verify folder paths in **Scan Paths** |
| Plugin appears but produces no sound | Check `%APPDATA%\AmpForge\host.log` for errors |
| "Couldn't open input device" | Connect your audio interface, then open **Audio Settings** and select an available device |
| No audio output | Check MUTE, In Gain, Out Vol; check section / slot bypass state; check **Audio Settings** |
| Control bindings lost after loading an old preset | Re-learn bindings once — old preset files did not store slot IDs |
| VST2 plugins not detected | Requires the Steinberg VST2 SDK placed at `vst2sdk/` in the project root, then rebuild |

**Log file:** `%APPDATA%\AmpForge\host.log` — all messages are prefixed with `[AmpForge]`.

---

## File Locations

| File | Path |
|------|------|
| Log | `%APPDATA%\AmpForge\host.log` |
| Presets | `%APPDATA%\AmpForge\presets\*.tfpreset` |
| Settings (device, templates, maps) | `%APPDATA%\AmpForge\` |
| Host executable | `build/AmpForge_artefacts/Debug/Amp Forge.exe` |
| Scan worker | `build/AmpForge_artefacts/Debug/AmpForgeScanWorker.exe` |
