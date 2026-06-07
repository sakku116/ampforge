# Guitar MultiFX Simulator

Aplikasi desktop untuk memproses gitar secara real-time menggunakan plugin VST3/VST2. Berjalan di Windows.

Dirancang sebagai pedalboard virtual — kamu bisa menyusun plugin efek dalam urutan yang kamu mau, mengatur bypass per-pedal, menyimpan preset, dan mengikat tombol MIDI atau keyboard ke aksi tertentu agar bisa dikontrol saat manggung.

---

## Fitur Utama

- **Signal chain** — susun plugin VST3/VST2 secara seri; drag-and-drop untuk ubah urutan
- **Sections** — kelompokkan plugin menjadi *Stomp* (bypass independen) atau *Preset* (satu aktif sekaligus, seperti channel amp)
- **Master controls** — input gain, output volume, dan mute global
- **Templates** — simpan dan recall beberapa konfigurasi chain dengan nama
- **Control mapping** — ikat keyboard atau MIDI (note/CC/footswitch) ke bypass/switch plugin
- **Expression pedal** — ikat CC MIDI ke parameter plugin manapun
- **Preset file** — simpan/muat snapshot chain ke file `.tfpreset`
- **Scan aman** — plugin discan di proses terpisah agar plugin rusak tidak mematikan aplikasi

---

## Cara Pakai

Panduan lengkap ada di [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

Singkatnya:
1. Build project (lihat bagian Setup di bawah)
2. Jalankan `Guitar VST3 Host.exe`
3. Plugin VST3 terdeteksi otomatis saat startup
4. Drag plugin dari Library ke Signal Chain
5. Mainkan gitar lewat audio interface kamu

---

## Setup

### Prasyarat

- **Windows 10/11**
- **Visual Studio 2022** dengan workload *Desktop development with C++*
- **CMake 3.22+**
- Koneksi internet saat pertama configure (JUCE di-download otomatis via CMake)

### Build

```powershell
# Clone repo
git clone <repo-url>
cd guitar-multifx-simulator

# Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Debug --parallel
```

Atau pakai Makefile shortcut:

```powershell
make build      # Debug
make release    # Release
make run        # Build + jalankan Debug
```

Executable ada di `build/GtrFxSim_artefacts/Debug/Guitar VST3 Host.exe`.

> **Catatan:** Dua file harus ada di folder yang sama — `Guitar VST3 Host.exe` dan `GuitarVST3ScanWorker.exe`. CMake mengurus salinannya secara otomatis saat build.

---

## Setup ASIO (Opsional, Direkomendasikan)

ASIO memberikan latency rendah yang jauh lebih baik dibanding WASAPI/DirectSound — sangat disarankan jika kamu pakai audio interface (Focusrite, Behringer, dll).

### 1. Download Steinberg ASIO SDK

Kunjungi halaman resmi Steinberg dan download ASIO SDK (gratis, butuh registrasi akun):

👉 https://www.steinberg.net/asiosdk/

Atau clone langsung:

```powershell
git clone https://github.com/audiosdk/asio.git asio
```

### 2. Letakkan SDK di salah satu lokasi ini

CMake mendeteksi secara otomatis dari beberapa lokasi:

| Lokasi | Keterangan |
|--------|------------|
| `asio/` di root project | Paling mudah, lokal per-project |
| `C:\ASIOSDK` | Global, bisa dipakai di banyak project |

Struktur folder yang diharapkan:
```
asio/
  common/
    asio.h
    asiodrivers.h
    ...
```

### 3. Rebuild CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
```

Kalau SDK terdeteksi, CMake akan menampilkan:
```
-- ASIO SDK found: <path>
```

Setelah build, buka **Audio Settings** di aplikasi dan pilih driver ASIO milik audio interface kamu.

> Folder `asio/` tidak di-commit ke repo ini (ada di `.gitignore`).

---

## Setup VST2 (Opsional)

Jika kamu punya plugin VST2 (format lama), dukungannya bisa diaktifkan dengan menambahkan Steinberg VST2 SDK.

> Steinberg sudah menghentikan distribusi resmi VST2 SDK. Kamu bisa mendapatkannya dari VST3 SDK yang menyertakan wrapper kompatibel di `public.sdk/source/vst2.x/`.

### Letakkan SDK di salah satu lokasi ini

| Lokasi | Keterangan |
|--------|------------|
| `vst2sdk/` di root project | Lokal per-project |
| `C:\VST2_SDK` | Global |

Struktur yang diharapkan:
```
vst2sdk/
  pluginterfaces/
    vst2.x/
      aeffect.h
      aeffectx.h
```

### Rebuild CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
```

Kalau SDK terdeteksi:
```
-- VST2 SDK found: <path>
```

---

## Troubleshooting

| Gejala | Solusi |
|--------|--------|
| Plugin tidak muncul | Klik **Rescan Plugins**; tambah folder kustom via **Scan Paths...** |
| Tidak ada suara | Cek Audio Settings, pastikan device input/output benar |
| Latency tinggi | Aktifkan ASIO (lihat setup di atas), kecilkan buffer size |
| "Couldn't open input device" | Sambungkan audio interface, atau buka Audio Settings |
| Plugin crash saat scan | Normal — scan worker mengisolasi crash; lihat log untuk detail |

**Log file:** `%APPDATA%\GtrFxSim\host.log`

---

## Struktur Project

```
src/              Seluruh source code C++
docs/             Dokumentasi tambahan (USER_GUIDE.md)
build/            Output build (di-generate CMake, tidak di-commit)
asio/             Steinberg ASIO SDK (opsional, tidak di-commit)
vst2sdk/          Steinberg VST2 SDK (opsional, tidak di-commit)
CMakeLists.txt    Konfigurasi build
Makefile          Shortcut build command
CLAUDE.md         Konteks project untuk AI assistant
```

---

## Lisensi

Untuk penggunaan pribadi. JUCE digunakan di bawah lisensinya sendiri (lihat [juce.com](https://juce.com/juce-privacy-policy)).
