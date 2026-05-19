# Guitar VST3 Host

Minimal desktop VST3 host untuk guitar processing, dibangun dengan JUCE dan C++20.

---

## Aplikasi Ini Sudah Bisa Apa

### 1. Realtime Audio Input → Plugin → Output
Sinyal dari audio interface atau microphone masuk, diproses oleh plugin VST3 yang sedang aktif, lalu langsung keluar ke output. Latency sepenuhnya dikontrol oleh ukuran buffer audio device yang dipilih sistem.

### 2. Scan Plugin VST3 Otomatis
Saat pertama buka atau klik tombol **Scan Default VST3 Folders**, aplikasi memindai folder standar Windows:
- `C:\Program Files\Common Files\VST3`
- `C:\Program Files (x86)\Common Files\VST3`

Semua plugin yang ditemukan langsung muncul di daftar.

### 3. Daftar Plugin Interaktif
Plugin yang berhasil discan ditampilkan dalam list dengan nama dan format plugin (misal `MyAmp [VST3]`). Klik untuk memilih sebelum load.

### 4. Load Plugin Pilihan
Klik **Load Selected Plugin** untuk memuat plugin yang dipilih dari daftar. Plugin langsung disiapkan dengan sample rate dan buffer size yang sedang aktif di audio device.

### 5. Buka UI Editor Plugin
Klik **Open Plugin Editor** untuk membuka jendela GUI bawaan plugin. Kamu bisa mengatur parameter (gain, EQ, distortion, reverb, dll.) dari sana. Jendela bisa ditutup kapan saja tanpa menghentikan pemrosesan audio.

### 6. Auto-Restore Plugin Terakhir
Setiap kali load berhasil, identifier plugin disimpan ke file settings lokal. Saat aplikasi dibuka kembali, plugin terakhir otomatis di-load tanpa perlu pilih manual lagi.

---

## Cara Pakai

1. Jalankan aplikasi dari `build/GuitarVST3Host_artefacts/Debug/Guitar VST3 Host.exe`
2. Klik **Scan Default VST3 Folders**
3. Pilih plugin dari daftar
4. Klik **Load Selected Plugin**
5. Mainkan gitar / masukkan sinyal audio
6. (Opsional) Klik **Open Plugin Editor** untuk buka UI plugin

---

## Prerequisites (Windows)

- Visual Studio 2026 (atau kompatibel) dengan workload:
  - Desktop development with C++
  - MSVC toolset
  - Windows SDK
- CMake 3.22+
- Koneksi internet saat pertama configure (JUCE diunduh otomatis via FetchContent)

---

## Build dari Source

```bash
# Configure
"/c/Program Files/CMake/bin/cmake.exe" -S . -B build -G "Visual Studio 18 2026" -A x64

# Build
"/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug --parallel
```

Build ulang hanya diperlukan jika ada perubahan kode. Untuk penggunaan sehari-hari langsung jalankan exe-nya saja.

---

## Struktur Project

| File | Tanggung Jawab |
|------|----------------|
| `src/AudioEngine.*` | Inisialisasi audio device, realtime callback, routing buffer |
| `src/PluginScanner.*` | Scan folder VST3 Windows, kelola daftar plugin |
| `src/PluginHost.*` | Load/unload plugin instance, proses audio, kelola jendela editor |
| `src/MainComponent.*` | UI utama, orkestrasi komponen, persistence settings |
| `src/main.cpp` | Entry point aplikasi, main window |
| `CMakeLists.txt` | Build system, JUCE dependency via FetchContent |
