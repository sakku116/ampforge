# Guitar MultiFX Simulator — Panduan Pengguna

Aplikasi desktop untuk memproses gitar secara real-time menggunakan plugin VST3/VST2. Berjalan di Windows.

---

## Tampilan Utama

```
┌──────────────────────────────────────────────────────────────┐
│ [Audio Settings]  [Rescan Plugins]  [Scan Paths...]  CPU/DSP │
├──────────────────────────┬───────────────────────────────────┤
│  LIBRARY                 │  SIGNAL CHAIN                     │
│                          │  ┌─────────────────────────────┐  │
│  Distortion XY    VST3   │  │ ▼ Stomp 1         [⊘] [↑↓×] │  │
│  Reverb ABC       VST3   │  │   Distortion XY  [B]  [×]   │  │
│  Chorus ZZZ       VST3   │  │   Reverb ABC     [B]  [×]   │  │
│  ...                     │  ├─────────────────────────────┤  │
│                          │  │ ▼ Presets         [⊘] [↑↓×] │  │
│  [+ Add to Chain]        │  │   ○ Lead          [×]        │  │
│                          │  │   ● Crunch        [×]        │  │
│                          │  │   ○ Clean         [×]        │  │
│                          │  └─────────────────────────────┘  │
│                          │  [+ Stomp]  [+ Preset]            │
├──────────────────────────┴───────────────────────────────────┤
│  [Save]  [Load]                                              │
│  TEMPLATES: [Dropdown ▼] [◀] [▶] [⊕] [↑] [✎] [✕]  ●       │
│  MASTER:  In Gain [━━━━━━] Out Vol [━━━━━━] [MUTE]          │
│  CONTROL: [status]  [Learn Expression]  [Clear Maps]         │
└──────────────────────────────────────────────────────────────┘
```

---

## Library (Panel Kiri)

Daftar semua plugin VST3/VST2 yang terdeteksi di komputer.

- **Rescan Plugins** — scan ulang folder default dan folder kustom. Aman dipakai kapan saja; setiap plugin discan di proses terpisah agar plugin rusak tidak mematikan aplikasi.
- **Scan Paths...** — tambah / hapus folder kustom yang ikut discan (selain folder Windows default).
- **+ Add to Chain** — tambah plugin yang dipilih ke signal chain (atau double-click nama plugin).

Folder default yang discan:
- `C:\Program Files\Common Files\VST3`
- `C:\Program Files (x86)\Common Files\VST3`

---

## Signal Chain (Panel Kanan)

Urutan pemrosesan audio: Input → Slot 0 → Slot 1 → ... → Output.

### Section Header

Setiap bagian signal chain memiliki header dengan nama section dan tombol kontrol:

| Tombol | Fungsi |
|--------|--------|
| `⊘` | Bypass section — matikan semua plugin dalam section ini sekaligus |
| `↑` / `↓` | Pindah urutan section ke atas / bawah |
| `×` | Hapus section beserta semua plugin di dalamnya |

Nama section bisa diganti: **klik kanan** header → **Rename**.

### Tipe Section

**Stomp** — Setiap plugin punya toggle bypass (`B`) independen. Mirip stompbox di pedalboard fisik.

**Preset** — Hanya satu plugin aktif pada satu waktu (radio/exclusive). Berguna untuk amp sim atau multi-FX yang saling eksklusif. Plugin aktif ditandai `●`, yang lain `○`. Klik `●`/`○` untuk switch.

### Slot Plugin (Baris Plugin)

| Elemen | Fungsi |
|--------|--------|
| Nama plugin | Klik untuk membuka editor UI plugin |
| `B` (Stomp) | Toggle bypass plugin ini |
| `●`/`○` (Preset) | Aktifkan slot ini (matikan slot lain di section yang sama) |
| Label binding | Menampilkan key/CC yang terikat (contoh: `Key 1`, `CC 7`) |
| `×` | Hapus plugin dari chain |
| Drag | Seret baris untuk mengubah urutan atau pindah section |

**Klik kanan** pada baris plugin untuk opsi tambahan:
- **Learn Control** — ikat key keyboard atau MIDI CC/note ke slot ini
- **Rename** — beri nama kustom pada slot
- **Open Editor** — buka UI plugin

### Menambah / Menghapus Section

- **+ Stomp** — tambah section bertipe Stomp baru di bawah
- **+ Preset** — tambah section bertipe Preset baru di bawah

---

## Master Controls (Footer, baris ke-3)

| Kontrol | Fungsi |
|---------|--------|
| **In Gain** slider | Gain input sebelum masuk plugin chain (0–200%) |
| **Out Vol** slider | Volume output setelah plugin chain |
| **MUTE** | Matikan output audio sepenuhnya (input tetap jalan) |

---

## Preset (Save / Load)

Menyimpan dan memuat snapshot seluruh signal chain ke file `.tfpreset`.

- **Save** — simpan chain saat ini (pilih lokasi file)
- **Load** — muat file `.tfpreset` yang sudah ada

File preset menyimpan: daftar section, urutan plugin, state setiap plugin, status bypass, dan binding kontrol (slotId stabil — binding tidak rusak meski urutan section diubah).

Default folder preset: `%APPDATA%\GtrFxSim\presets\`

---

## Templates

Template adalah snapshot bernama dari seluruh signal chain. Berbeda dari preset file: template disimpan di dalam aplikasi (tidak perlu pilih file saat recall), cocok untuk quick-switch antar setup.

| Kontrol | Fungsi |
|---------|--------|
| Dropdown | Pilih template aktif |
| `◀` / `▶` | Pindah ke template sebelumnya / berikutnya |
| `⊕` | Simpan chain saat ini sebagai template baru |
| `↑` | Update template aktif dengan chain saat ini |
| `✎` | Rename template aktif |
| `✕` | Hapus template aktif |
| `●` (indicator) | Chain saat ini berbeda dari template yang tersimpan |

---

## Control Mapping

Ikat key keyboard atau MIDI (note/CC/program) ke aksi tertentu.

### Cara Belajar Binding (Learn)

1. **Klik kanan** pada baris plugin di signal chain
2. Pilih **Learn Control**
3. Status bar CONTROL menampilkan "Waiting for input..."
4. Tekan tombol keyboard, injak footswitch MIDI, atau gerakkan MIDI CC
5. Binding tersimpan otomatis

Aksi yang akan terikat tergantung tipe section:
- **Stomp section** → toggle bypass plugin tersebut
- **Preset section** → aktifkan / switch ke slot tersebut

Setelah binding tersimpan, label kecil (contoh: `Key Q`, `CC 64`) muncul di baris plugin.

### Bindings lainnya

Aksi lain (next template, prev template, load template) bisa dikonfigurasi melalui sistem yang sama tapi belum punya UI khusus — dikontrol lewat kode atau masa depan.

### Expression Pedal

- Klik **Learn Expression** → pilih slot dan parameter di editor plugin → gerakkan pedal MIDI CC → terikat.
- Pedal CC langsung mengontrol parameter plugin 0–100%.

### Clear Maps

Klik **Clear Maps** — hapus semua binding sekaligus. Label binding di baris plugin langsung hilang.

> **Catatan:** Binding menggunakan `slotId` stabil, bukan posisi. Mengubah urutan section atau memindah plugin tidak merusak binding yang sudah ada.

---

## Audio Settings

Klik **Audio Settings** di header untuk:
- Pilih driver audio (WASAPI / DirectSound / ASIO jika SDK tersedia)
- Pilih device input dan output
- Atur sample rate dan buffer size

State audio device disimpan otomatis.

---

## Troubleshooting

| Gejala | Solusi |
|--------|--------|
| Plugin tidak muncul di library | Klik **Rescan Plugins**; pastikan path folder sudah benar di **Scan Paths** |
| Plugin muncul tapi tidak jalan | Cek log di `%APPDATA%\GtrFxSim\host.log` |
| "Couldn't open input device" | Sambungkan audio interface; atau buka Audio Settings dan pilih device yang ada |
| Tidak ada suara | Cek MUTE, In Gain, Out Vol; cek bypass section/slot; cek Audio Settings |
| Binding kontrol rusak setelah load preset lama | Re-learn binding satu kali (file lama tidak menyimpan slotId) |
| VST2 tidak terdeteksi | Butuh Steinberg VST2 SDK ditempatkan di `vst2sdk/` lalu rebuild |

**Log file:** `%APPDATA%\GtrFxSim\host.log` — semua pesan dengan prefix `[GuitarHost]`.

---

## Shortcut Keyboard

Binding keyboard dikonfigurasi via **Learn Control** (lihat bagian Control Mapping). Tidak ada shortcut global bawaan kecuali yang sudah di-learn.

---

## Lokasi File Penting

| File | Lokasi |
|------|--------|
| Log | `%APPDATA%\GtrFxSim\host.log` |
| Preset | `%APPDATA%\GtrFxSim\presets\*.tfpreset` |
| Settings (device, templates, maps) | `%APPDATA%\GtrFxSim\` (ApplicationProperties) |
| Executable host | `build/GtrFxSim_artefacts/Debug/Guitar VST3 Host.exe` |
| Scan worker | `build/GtrFxSim_artefacts/Debug/GuitarVST3ScanWorker.exe` |
