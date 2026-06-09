# Amp Forge

A real-time guitar multi-effects processor for Windows, built with JUCE and C++20. Hosts VST3 and VST2 plugins in a flexible, pedalboard-style signal chain.

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
2. Run `Amp Forge.exe`
3. VST3 plugins are detected automatically on startup
4. Drag plugins from the Library into the Signal Chain
5. Play guitar through your audio interface

---

## Setup

### Requirements

- **Windows 10 or 11**
- **Visual Studio 2022 or 2026** with the *Desktop development with C++* workload
- **CMake 3.22+**
- Internet connection on first configure (JUCE is downloaded automatically via CMake)

### Build

Both ASIO and VST2 support are **bundled in this repository** — no additional downloads required.

```powershell
# Clone the repo
git clone https://github.com/YOUR_USERNAME/guitar-multifx-simulator
cd guitar-multifx-simulator

# Configure
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
# or for Visual Studio 2022:
# cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Debug --parallel
```

Or use the Makefile shortcuts:

```powershell
make build      # Debug build
make release    # Release build
make run        # Build and run (Debug)
```

A successful configure will print:
```
-- ASIO enabled. SDK: .../ASIOSDK
-- VST2 enabled. SDK: .../vst2sdk
```

The executable is at `build/AmpForge_artefacts/Debug/Amp Forge.exe`.

> **Note:** Two files must exist in the same folder — `Amp Forge.exe` and `AmpForgeScanWorker.exe`. CMake copies the worker automatically after every build.

---

## Troubleshooting

| Symptom | Solution |
|---------|----------|
| Plugins not showing up | Click **Rescan Plugins**; add custom folders via **Scan Paths...** |
| No audio output | Check Audio Settings — make sure the correct input/output device is selected |
| High latency | Enable ASIO and reduce buffer size in Audio Settings |
| "Couldn't open input device" | Connect your audio interface, or open Audio Settings |
| Plugin crashes during scan | Expected — the scan worker isolates crashes; check the log for details |

**Log file:** `%APPDATA%\AmpForge\host.log`

---

## Project Structure

```
src/              C++ source code
docs/             User guide
ASIOSDK/          Steinberg ASIO SDK 2.3 (bundled, GPL v3)
vst2sdk/          VST2 interface headers (bundled, MIT clean-room)
build/            Build output (generated, not committed)
CMakeLists.txt    Build configuration
Makefile          Build shortcuts
```

---

## License

This project is licensed under the **GNU Affero General Public License v3.0 (AGPLv3)** — see [LICENSE](LICENSE) for the full text.

| Component | License | Notes |
|-----------|---------|-------|
| This project | AGPLv3 — [LICENSE](LICENSE) | |
| JUCE | AGPLv3 option | See [juce.com/get-juce](https://juce.com/get-juce/) |
| Steinberg ASIO SDK 2.3 (`ASIOSDK/`) | GPL v3 option | See [ASIOSDK/LICENSE.txt](ASIOSDK/LICENSE.txt) |
| VST2 interface headers (`vst2sdk/`) | MIT | Clean-room implementation — see [vst2sdk/LICENSE](vst2sdk/LICENSE) |
| Steinberg VST3 SDK | MIT | Bundled within JUCE via CMake FetchContent |

### VST2 hosting note

The `vst2sdk/` headers are a clean-room implementation of the VST2 binary ABI — not derived from Steinberg's source code. Hosting VST2 plugins for interoperability purposes is permitted under reverse-engineering-for-interoperability provisions in most jurisdictions (US DMCA § 1201(f), EU Software Directive Article 6(2)).

"VST" is a trademark of Steinberg Media Technologies GmbH. This project is not affiliated with or endorsed by Steinberg.
