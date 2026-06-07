# Guitar MultiFX Simulator

A real-time guitar multi-effects processor for Windows, built with JUCE and C++20. Hosts VST3 (and optionally VST2) plugins in a flexible, pedalboard-style signal chain.

Designed to replace a physical pedalboard on a desktop — arrange effects in any order, toggle bypass per pedal, save presets, and bind MIDI footswitches or keyboard keys to actions for live use.

---

## Features

- **Signal chain** — load VST3/VST2 plugins in series; drag and drop to reorder
- **Sections** — group plugins into *Stomp* (independent bypass) or *Preset* (one active at a time, like amp channels)
- **Master controls** — input gain, output volume, and global mute
- **Templates** — save and recall multiple named chain configurations
- **Control mapping** — bind keyboard keys or MIDI (note/CC/footswitch) to bypass or switch plugins
- **Expression pedal** — map a MIDI CC to any plugin parameter
- **Preset files** — save/load full chain snapshots to `.tfpreset` files
- **Safe scanning** — each plugin is scanned in a separate process so a crashing plugin cannot take down the host

---

## Usage

See [docs/USER_GUIDE.md](docs/USER_GUIDE.md) for the full guide.

Quick start:
1. Build the project (see [Setup](#setup) below)
2. Run `Guitar VST3 Host.exe`
3. VST3 plugins are detected automatically on startup
4. Drag plugins from the Library into the Signal Chain
5. Play guitar through your audio interface

---

## Setup

### Requirements

- **Windows 10 or 11**
- **Visual Studio 2022** with the *Desktop development with C++* workload
- **CMake 3.22+**
- Internet connection on first configure (JUCE is downloaded automatically via CMake)

### Build

```powershell
# Clone the repo
git clone <repo-url>
cd guitar-multifx-simulator

# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Debug --parallel
```

Or use the Makefile shortcuts:

```powershell
make build      # Debug build
make release    # Release build
make run        # Build and run (Debug)
```

The executable is at `build/GtrFxSim_artefacts/Debug/Guitar VST3 Host.exe`.

> **Note:** Two files must exist in the same folder — `Guitar VST3 Host.exe` and `GuitarVST3ScanWorker.exe`. CMake copies the worker automatically after every build.

---

## ASIO Setup (Optional, Recommended)

ASIO provides significantly lower latency than WASAPI/DirectSound — strongly recommended if you use an audio interface (Focusrite, Behringer, etc.).

### 1. Download the Steinberg ASIO SDK

Get it from the official Steinberg page (free, requires account registration):

👉 https://www.steinberg.net/asiosdk/

Or clone directly:

```powershell
git clone https://github.com/audiosdk/asio.git asio
```

### 2. Place the SDK in one of these locations

CMake detects it automatically from several locations:

| Location | Notes |
|----------|-------|
| `asio/` in the project root | Easiest — local to this project |
| `C:\ASIOSDK` | Global — shared across projects |

Expected folder structure:
```
asio/
  common/
    asio.h
    asiodrivers.h
    ...
```

### 3. Reconfigure and rebuild

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
```

If the SDK is detected, CMake will print:
```
-- ASIO SDK found: <path>
```

After building, open **Audio Settings** in the app and select your audio interface's ASIO driver.

> The `asio/` folder is not committed to this repo (listed in `.gitignore`).

---

## VST2 Setup (Optional)

If you have VST2 plugins (older format), support can be enabled by providing the Steinberg VST2 SDK.

> Steinberg has discontinued official VST2 SDK distribution. You can obtain it from the VST3 SDK which includes a compatibility wrapper under `public.sdk/source/vst2.x/`.

### Place the SDK in one of these locations

| Location | Notes |
|----------|-------|
| `vst2sdk/` in the project root | Local to this project |
| `C:\VST2_SDK` | Global |

Expected structure:
```
vst2sdk/
  pluginterfaces/
    vst2.x/
      aeffect.h
      aeffectx.h
```

### Reconfigure and rebuild

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
```

If the SDK is detected:
```
-- VST2 SDK found: <path>
```

---

## Troubleshooting

| Symptom | Solution |
|---------|----------|
| Plugins not showing up | Click **Rescan Plugins**; add custom folders via **Scan Paths...** |
| No audio output | Check Audio Settings — make sure the correct input/output device is selected |
| High latency | Enable ASIO (see setup above) and reduce buffer size |
| "Couldn't open input device" | Connect your audio interface, or open Audio Settings |
| Plugin crashes during scan | Expected — the scan worker isolates crashes; check the log for details |

**Log file:** `%APPDATA%\GtrFxSim\host.log`

---

## Project Structure

```
src/              All C++ source code
docs/             Additional documentation (USER_GUIDE.md)
build/            Build output (generated by CMake, not committed)
asio/             Steinberg ASIO SDK (optional, not committed)
vst2sdk/          Steinberg VST2 SDK (optional, not committed)
CMakeLists.txt    Build configuration
Makefile          Build command shortcuts
CLAUDE.md         Project context for AI assistants
```

---

## License

For personal use. JUCE is used under its own license — see [juce.com](https://juce.com/juce-privacy-policy).
