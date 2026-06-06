# Signal Chain — Sections (Stomp & Preset)

## Gambaran Umum

Signal chain adalah jalur pemrosesan audio berurutan:

```
Audio In → [Slot 0] → [Slot 1] → ... → [Slot N] → Audio Out
```

Setiap slot berisi satu plugin VST3. Slot-slot dikelompokkan ke dalam **section**. Section menentukan bagaimana tombol bypass di setiap slot berperilaku.

---

## Jenis Section

### Stomp Section

Setiap plugin di-bypass atau diaktifkan secara **independen** — perilaku default seperti pedalboard biasa.

```
Section: Stomp 1
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Compressor│  │ Overdrive│  │  Reverb  │
│  [ON]    │  │  [BYP]   │  │  [ON]    │
└──────────┘  └──────────┘  └──────────┘
```

- Menekan `ON` pada Overdrive → hanya Overdrive yang menyala, yang lain tidak terpengaruh.
- Menekan `BYP` pada Compressor → hanya Compressor yang di-bypass.

### Preset Section

Hanya **satu** plugin yang aktif dalam satu waktu — seperti efek amp yang bekerja secara eksklusif. Mengaktifkan slot X secara otomatis me-bypass semua slot lain dalam section yang sama.

```
Section: Preset 1
┌──────────┐  ┌──────────┐  ┌──────────┐
│ Amp Clean│  │ Amp Crunch│  │ Amp Lead │
│  [ACT]   │  │  [SEL]   │  │  [SEL]   │
└──────────┘  └──────────┘  └──────────┘
     ↑ hanya ini yang active
```

- Menekan `SEL` pada "Amp Crunch" → Amp Crunch menjadi active (`ACT`), Amp Clean dan Amp Lead otomatis ter-bypass.
- Tidak ada cara untuk mematikan semua slot sekaligus dalam preset section; selalu ada tepat satu yang aktif.

---

## Model Data

### Hierarki

```
PluginChain
├── sectionDefs: vector<SectionDef>      ← urutan dan metadata section
│   ├── SectionDef { id=1, name="Stomp 1", type=stomp }
│   ├── SectionDef { id=2, name="Preset 1", type=preset }
│   └── SectionDef { id=3, name="Stomp 2", type=stomp }
│
└── activeList: shared_ptr<SlotList>     ← flat list, diakses audio thread
    ├── Slot { sectionId=1, bypassed=false, instance=Compressor }
    ├── Slot { sectionId=1, bypassed=false, instance=Overdrive  }
    ├── Slot { sectionId=2, bypassed=false, instance=Amp Clean  }
    ├── Slot { sectionId=2, bypassed=true,  instance=Amp Crunch }
    ├── Slot { sectionId=2, bypassed=true,  instance=Amp Lead   }
    └── Slot { sectionId=3, bypassed=false, instance=Delay      }
```

### Struct Utama

**`SectionDef`** — definisi satu section (message thread only):
```cpp
struct SectionDef {
    enum class Type { stomp, preset };
    int          id;     // unik, tidak pernah berubah setelah dibuat
    juce::String name;   // "Stomp 1", "Preset 1", dll.
    Type         type;
};
```

**`Slot`** (private, internal) — satu instance plugin:
```cpp
struct Slot {
    std::unique_ptr<AudioPluginInstance> instance;
    std::atomic<bool> bypassed;   // dibaca audio thread — harus atomic
    PluginDescription description;
    juce::String customName;      // message thread only
    int sectionId;                // message thread only
};
```

**`SlotSpec`** — snapshot untuk serialisasi (preset/scene):
```cpp
struct SlotSpec {
    PluginDescription description;
    MemoryBlock state;       // state internal plugin (base64 di XML)
    bool bypassed;
    juce::String customName;
    int sectionId;
};
```

**`SlotInfo`** — data read-only untuk UI:
```cpp
struct SlotInfo {
    juce::String name;         // customName jika ada, otherwise plugin name
    juce::String originalName; // selalu nama asli plugin
    juce::String format;       // "VST3"
    bool bypassed;
    bool hasCustomName;
    int  sectionId;
    bool isPreset;             // true jika section bertipe preset
};
```

---

## Invariant Penting

### 1. Slot tersusun berurutan per section

Dalam `SlotList`, semua slot milik section A selalu berurutan bersama sebelum slot section B, mengikuti urutan `sectionDefs`. Tidak boleh ada slot section B "menyelip" di antara slot section A.

```
// Benar ✓
[A(s1), A(s1), B(s2), B(s2), C(s3)]

// Salah ✗
[A(s1), B(s2), A(s1), B(s2), C(s3)]
```

Invariant ini dijaga oleh:
- `addPlugin(desc, sectionId)` — mencari posisi insert yang tepat
- `moveSectionUp/Down()` — rebuild `SlotList` dengan urutan baru
- `movePlugin(from, to)` — ketika lintas section, update `sectionId` slot ke section tujuan

### 2. Audio thread tidak tahu tentang sections

`processAudio()` hanya membaca `slot->bypassed`. Section, `sectionId`, dan `sectionDefs` tidak pernah diakses dari audio thread. Semua section logic berjalan di **message thread** saja.

```cpp
// processAudio — sesederhana ini:
for (const auto& slot : list)
{
    if (slot->bypassed.load())   // atomic read — satu-satunya yang dilihat audio thread
        continue;
    slot->instance->processBlock(buffer, midi);
}
```

### 3. `activatePresetSlot` tidak butuh republish

Karena `bypassed` adalah `std::atomic<bool>`, mengubahnya tidak memerlukan rebuild `SlotList`. Ini membuatnya **lock-free dan aman dari message thread** tanpa mengganggu audio thread yang sedang memproses.

---

## Alur Operasi

### Menambah Plugin ke Section

```
User klik section header → row header ter-select
                               ↓
User double-click library / klik "+ Add to Chain"
                               ↓
MainComponent::addSelectedToChain()
    → getTargetSectionId()   // baca selected row → ambil sectionId
    → pluginHost.addPlugin(description, sectionId)
                               ↓
PluginChain::addPlugin(desc, sectionId)
    → createSlot()           // buat instance VST3 (di luar lock, bisa lambat)
    → cari insertPos         // setelah slot terakhir di section ini
    → publish(next)          // atomic swap SlotList
                               ↓
MainComponent::refreshChainList()
    → rebuild ChainRow list  // header + slot rows per section
    → chainListBox.updateContent()
```

### Mengaktifkan Preset Slot

```
User klik "SEL" pada slot di Preset section
                    ↓
ChainSlotComponent::bypassButton.onClick
    → model.onBypass(slotIndex)
                    ↓
MainComponent::toggleBypassAt(slotIndex)
    → cek info.isPreset == true
    → activatePresetSlotAt(slotIndex)
                    ↓
PluginChain::activatePresetSlot(slotIndex)
    → verifikasi section.type == preset
    → for each slot in same section:
          slot->bypassed.store(i != slotIndex)   // atomic, no republish
                    ↓
refreshChainList() → UI update
```

### Memindahkan Slot Lintas Section

```
User klik ▼ pada slot terakhir di "Stomp 1"
                    ↓
moveSlotAt(slotIndex, +1)
                    ↓
PluginChain::movePlugin(from, to)
    → newSectionId = cur[to]->sectionId   // ambil section slot tujuan
    → erase from, insert at to
    → next[to]->sectionId = newSectionId  // slot masuk ke section baru
    → publish(next)
                    ↓
refreshChainList() → slot muncul di section tujuan
```

### Memindahkan Seluruh Section

```
User klik ▲ di SectionHeaderComponent
                    ↓
model.onSectionMoveUp(sectionId)
                    ↓
PluginChain::moveSectionUp(sectionId)
    → swap adjacent entries di sectionDefs
    → rebuild SlotList: untuk setiap section (urutan baru),
      kumpulkan semua slot-nya lalu gabungkan
    → publish(reorderedList)
                    ↓
refreshChainList() → seluruh section beserta isinya berpindah
```

---

## UI: ChainRow dan Rendering

`refreshChainList()` di `MainComponent` membangun array `ChainRow` yang merepresentasikan apa yang ditampilkan di list box:

```
sectionDefs = [Stomp 1, Preset 1, Stomp 2]
slotInfos   = [Comp(s1), OD(s1), Clean(s2), Crunch(s2), Delay(s3)]

ChainRow array yang dihasilkan:
[0] sectionHeader  { id=1, name="Stomp 1",  type=stomp,  isFirst=true  }
[1] slot           { slotInfo=Comp,   slotIndex=0, isFirst=true,  isLast=false }
[2] slot           { slotInfo=OD,     slotIndex=1, isFirst=false, isLast=true  }
[3] sectionHeader  { id=2, name="Preset 1", type=preset, isFirst=false }
[4] slot           { slotInfo=Clean,  slotIndex=2, isFirst=true,  isLast=false }
[5] slot           { slotInfo=Crunch, slotIndex=3, isFirst=false, isLast=false }
[6] sectionHeader  { id=3, name="Stomp 2",  type=stomp,  isFirst=false, isLast=true }
[7] slot           { slotInfo=Delay,  slotIndex=4, isFirst=true,  isLast=true  }
```

`refreshComponentForRow()` membuat komponen yang tepat per row:
- `ChainRow::Kind::sectionHeader` → `SectionHeaderComponent`
- `ChainRow::Kind::slot` → `ChainSlotComponent`

Tombol ▲▼ pada slot:
- Diaktifkan berdasarkan `slotIndex > 0` dan `slotIndex < totalSlots - 1`
- Bisa memindahkan slot lintas section (bukan hanya dalam section yang sama)

Tombol ▲▼ pada section header:
- Diaktifkan berdasarkan `isFirstSection` dan `isLastSection`
- Memindahkan seluruh section beserta isinya

---

## Serialisasi

### Format XML Preset (versi 2)

```xml
<TONEFORGE_PRESET name="My Preset" version="2">

  <!-- Section definitions (urutan penting) -->
  <SECTION sectionId="1" sectionName="Stomp 1" sectionType="stomp"/>
  <SECTION sectionId="2" sectionName="Preset 1" sectionType="preset"/>
  <SECTION sectionId="3" sectionName="Stomp 2" sectionType="stomp"/>

  <!-- Slots (urutan = urutan dalam flat list) -->
  <SLOT sectionId="1" bypassed="false">
    <PLUGIN name="Compressor" .../>
  </SLOT>
  <SLOT sectionId="1" bypassed="false">
    <PLUGIN name="Overdrive" .../>
  </SLOT>
  <SLOT sectionId="2" bypassed="false">
    <PLUGIN name="Amp Clean" .../>
  </SLOT>
  <SLOT sectionId="2" bypassed="true">
    <PLUGIN name="Amp Crunch" .../>
  </SLOT>

</TONEFORGE_PRESET>
```

### Migrasi dari Format Lama (versi 1)

File preset lama yang tidak memiliki node `<SECTION>` secara otomatis di-migrate:
- Semua slot dimasukkan ke section default `{ id=1, name="Stomp 1", type=stomp }`
- Tidak ada perubahan pada perilaku audio

```cpp
// Di Preset::fromValueTree():
if (outSections.isEmpty())
    outSections.add({ 1, "Stomp 1", PluginChain::SectionDef::Type::stomp });
```

### Scene

Scene menyimpan snapshot section bersama dengan slot specs. Ketika scene di-recall, sections dan slots di-restore bersamaan via `switchChainWithCrossfade(specs, sections, ms)`.

---

## Ringkasan Tanggung Jawab Setiap Layer

| Layer | File | Tanggung Jawab |
|---|---|---|
| **Audio** | `PluginChain::processAudio` | Hanya baca `bypassed` atomic — tidak tahu sections |
| **Data** | `PluginChain` | Simpan `sectionDefs` + `SlotList`; enforce ordering invariant |
| **Bypass logic** | `PluginChain::activatePresetSlot` | Exclusive activate untuk preset section |
| **Persistence** | `Preset`, `SceneManager` | Serialize/deserialize sections + slots; migrasi v1→v2 |
| **MIDI control** | `ControlMap`, `ControlAction` | `toggleBypass` untuk stomp, `activatePresetSlot` untuk preset |
| **UI model** | `ChainListModel`, `ChainRow` | Flatten sections+slots menjadi flat row list untuk ListBox |
| **UI component** | `SectionHeaderComponent` | Render header, ▲▼ section, selection state |
| **UI component** | `ChainSlotComponent` | Render slot, bypass toggle, ▲▼ cross-section |
| **Orchestration** | `MainComponent` | Wiring semua callback; rebuild ChainRow setiap perubahan |
