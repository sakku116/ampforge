# Guitar VST3 Host

Minimal desktop VST3 host untuk guitar processing, dibangun dengan JUCE dan C++20.

---

## Fitur

| Fitur | Deskripsi |
|-------|-----------|
| **Audio realtime** | Input → plugin VST3 → output via `AudioDeviceManager` |
| **Scan VST3** | Folder default Windows; bundle folder dan file `.vst3` tunggal |
| **Scan aman** | Setiap plugin discan di subprocess (`GuitarVST3ScanWorker.exe`) agar plugin rusak tidak menjatuhkan host |
| **Daftar plugin** | List interaktif dengan nama + format |
| **Load plugin** | Satu plugin aktif; disiapkan sesuai sample rate / buffer device |
| **Editor plugin** | Tombol buka GUI bawaan plugin |
| **Audio settings** | Pilih driver / device (opsional) |
| **Persistence** | Plugin terakhir + state audio device disimpan ke `%APPDATA%\GtrFxSim\` |
| **Debug log** | File log persisten untuk troubleshooting |

---

## Cara Pakai

1. **Build** project (lihat [Build](#build-dari-source)).
2. Jalankan host (perhatikan tanda kutip karena ada spasi di nama exe):

```bash
"build/GtrFxSim_artefacts/Debug/Guitar VST3 Host.exe"
```

3. Saat startup, scan VST3 berjalan otomatis di background.
4. Pilih plugin di list → **Load Selected Plugin**.
5. Mainkan gitar / sinyal masuk (butuh input device yang tersedia).
6. (Opsional) **Open Plugin Editor** untuk UI plugin.
7. **Scan Default VST3 Folders** untuk scan ulang manual.

### Folder VST3 yang discan

- `C:\Program Files\Common Files\VST3`
- `C:\Program Files (x86)\Common Files\VST3`

Struktur yang didukung:

- **Bundle folder** — mis. `AmpliTube 5.vst3\` (direktori)
- **File tunggal** — mis. `Neural DSP\Archetype Tim Henson X.vst3` (DLL)

---

## Debug & Troubleshooting

### Log file

```
%APPDATA%\GtrFxSim\host.log
```

Contoh: `C:\Users\<user>\AppData\Roaming\GtrFxSim\host.log`

Semua pesan memakai prefix `[GuitarHost]`. Berguna saat scan gagal, load error, atau masalah audio device.

### Masalah umum

| Gejala | Kemungkinan penyebab | Solusi |
|--------|----------------------|--------|
| `Segmentation fault` saat scan (lama) | Plugin VST3 crash saat di-load untuk scan | Sudah diatasi dengan scan worker terpisah; pastikan `GuitarVST3ScanWorker.exe` ada di folder yang sama dengan host |
| Plugin tidak muncul di list | Scan worker hilang / path salah | Rebuild; cek log `Scan worker missing` |
| `Couldn't open the input device!` | Tidak ada mic/interface input | Host fallback ke **output only** (0 in / 2 out); buka **Audio Settings** atau sambungkan interface |
| Hanya beberapa plugin terlihat | Plugin lain gagal discan | Cek log untuk `scan failed` atau `scan CRASHED` per path |
| Bash: `Guitar: No such file` | Spasi di nama exe | Gunakan tanda kutip penuh seperti contoh di atas |

### Artefak build yang wajib ada

Setelah build Debug, di folder `build/GtrFxSim_artefacts/Debug/`:

| File | Peran |
|------|-------|
| `Guitar VST3 Host.exe` | Aplikasi utama (GUI) |
| `GuitarVST3ScanWorker.exe` | Worker scan VST3 (disalin otomatis saat build) |

---

## Prerequisites (Windows)

- Visual Studio 2022 / 2026 (atau kompatibel) dengan workload **Desktop development with C++**
- CMake 3.22+
- Koneksi internet saat pertama **configure** (JUCE di-fetch via FetchContent, tag `8.0.2`)

### ASIO (opsional)

Untuk mengaktifkan ASIO, clone SDK ke root project:

```bash
git clone https://github.com/audiosdk/asio.git asio
```

Atau letakkan [Steinberg ASIO SDK](https://www.steinberg.net/asiosdk/) di salah satu path:

- `asio/` di root project
- `C:/ASIOSDK`
- Atau configure dengan `-DASIO_SDK_DIR=<path>`

Folder `asio/` adalah dependency lokal dan tidak di-commit ke repo ini. Tanpa SDK, build tetap sukses (WASAPI / DirectSound).

---

## Quick Build & Run dengan Makefile

```bash
# Build Debug
make build

# Build Release
make release

# Run Debug build
make run

# Run Release build
make run-release

# Clean build folder
make clean

# Clean + rebuild
make rebuild

# Show help
make help
```

Lihat `Makefile` di root project untuk daftar semua target.

---

## Build dari Source (manual)

```bash
# Configure (sesuaikan generator dengan VS Anda)
"/c/Program Files/CMake/bin/cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64

# Build host + scan worker
"/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel
```

Target yang di-build:

- `GtrFxSim` — aplikasi GUI
- `GuitarVST3ScanWorker` — disalin ke folder output host sebagai `GuitarVST3ScanWorker.exe`

---

## Struktur Project

| File / modul | Tanggung jawab |
|--------------|----------------|
| `src/main.cpp` | Entry point GUI, setup log file |
| `src/MainComponent.*` | UI, tombol, persistence, orkestrasi |
| `src/AudioEngine.*` | `AudioDeviceManager`, callback realtime, routing buffer |
| `src/PluginScanner.*` | Enumerasi path VST3, panggil scan worker per file |
| `src/PluginHost.*` | Load/unload instance, `processBlock`, jendela editor |
| `src/ScanSubprocess.*` | Spawn worker, merge hasil XML ke `KnownPluginList` |
| `src/scan_worker_main.cpp` | Entry point console worker |
| `src/PluginScanGuard.*` | Perlindungan SEH saat load plugin (Windows) |
| `src/HostDebug.h` | Macro/helper log `[GuitarHost]` |
| `CMakeLists.txt` | JUCE FetchContent, dua target, copy worker POST_BUILD |

### Alur scan VST3

```text
MainComponent (UI thread)
  → PluginScanner::scanDefaultWindowsVST3Folders()
    → untuk setiap path .vst3:
        ScanSubprocess::scanFileInChildProcess()
          → ChildProcess: GuitarVST3ScanWorker.exe --scan-vst3="<path>"
          → worker: scanAndAddFile → stdout (XML KnownPluginList)
          → host: merge XML → KnownPluginList
```

---

## Dokumentasi tambahan

| Lokasi | Isi |
|--------|-----|
| `agent/plans/phase1.md` | Spesifikasi & task list phase 1 |
| `agent/plans/phase1-revision.md` | Status verifikasi & gap phase 1 |
| `.github/copilot-instructions.md` | Panduan arsitektur untuk AI assistant |
| `.github/instructions/juce-build.instructions.md` | Aturan build & implementasi JUCE |

Folder `agent/` dan `.github/` di-ignore oleh git (lihat `.gitignore`); tetap berguna secara lokal untuk Cursor / Copilot.
