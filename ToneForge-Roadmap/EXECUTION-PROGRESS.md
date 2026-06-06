# ToneForge Execution Progress

Checkpoint resumable untuk eksekusi roadmap Phase 2 → 4. **File ini adalah sumber kebenaran**: di-update + di-commit setiap satu task selesai.

## ▶️ CARA RESUME (untuk sesi Claude baru, mis. setelah limit reset)
1. Baca file ini dari atas.
2. Jalankan `git branch --show-current` & `git status --short`.
3. Cek "State" di bawah → cocokkan branch & last good commit.
4. Cari task pertama yang **bukan `[x]`** → itu task berikutnya.
5. Jika ada task `[~]` (in-progress) dan working tree kotor: selesaikan atau `git checkout -- .` agar bersih dulu.
6. Lanjut eksekusi. Setiap task selesai: build hijau → update checkbox di sini → `git commit` (kode + file ini di commit yang sama).
7. Aturan emas: **commit hanya saat build hijau**; satu task = satu commit; jangan tinggalkan working tree tak ter-build.

Trigger user: cukup ketik **"lanjut eksekusi toneforge"**.

## Build
- Generator: `Visual Studio 18 2026` (x64). JUCE 8.0.2 sudah ter-download di `build/_deps/juce-src`.
- Configure (sekali, sudah dilakukan): `cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE="<repo>/build/_deps/juce-src"`
- Build per task: `cmake --build build --config Debug --parallel`
- Catatan: project pernah dipindah path → cache lama stale; sudah di-reconfigure.

## State
- Current branch: `feat/phase-2-pedalboard`
- Last good commit: (lihat `git log --oneline -1`)
- Next task: 3.6 (switch-time instrumentation + merge) — on branch feat/phase-3-performance
- Build status: GREEN (3.5 metrics UI bar)

## Konvensi
- Branch per phase: `feat/phase-2-pedalboard`, `feat/phase-3-performance`, `feat/phase-4-live-control`. Merge ke `main` saat phase selesai & build hijau.
- Commit message akhiri dengan: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
- Verifikasi: build hijau = gate utama (tanpa audio hardware). Item perceptual/hardware ditandai 🔸 butuh sign-off user.

---

## Phase 2 — Pedalboard  (branch `feat/phase-2-pedalboard`)
- [x] 2.0 Branch + EXECUTION-PROGRESS.md
- [x] 2.1 `PluginChain` class (src/PluginChain.h/.cpp) + daftarkan ke CMake
- [x] 2.2 Refactor `PluginHost` → bungkus `PluginChain` (AudioEngine API tetap)
- [x] 2.3 UI pedalboard (palette + chain rows: bypass/up/down/remove/editor)
- [x] 2.4 Preset model (serialize chain + state plugin → ValueTree/XML)
- [x] 2.5 UI Save/Load preset + last-preset persistence
- [x] 2.6 Build hijau + smoke test + merge ke main

## Phase 3 — Performance  (branch `feat/phase-3-performance`)
- [x] 3.1 Chain swap realtime-safe (atomic shared_ptr; hapus CriticalSection dari audio path)
- [x] 3.2 Crossfade switching (old/new paralel, gain ramp)
- [x] 3.3 Preloaded presets (prepare di awal, swap atomik <50ms)
- [x] 3.4 Monitoring CPU / DSP load / latency
- [x] 3.5 UI metrics bar (Timer)
- [~] 3.6 Instrumentasi switch-time + build hijau + merge

## Phase 4 — Live Control  (branch `feat/phase-4-live-control`)
- [ ] 4.1 MIDI input (device enable, MidiMessageCollector, inject ke processBlock)
- [ ] 4.2 ControlMap (trigger→action) + learn mode
- [ ] 4.3 Keyboard mapping (KeyListener)
- [ ] 4.4 Footswitch (reuse ControlMap)
- [ ] 4.5 Scenes (snapshot pedalboard, switch via crossfade)
- [ ] 4.6 Expression pedal (CC → parameter plugin)
- [ ] 4.7 UI live-control + build hijau + merge

---

## 🔸 Butuh sign-off user (tak bisa diverifikasi tanpa hardware/telinga)
- Phase 1: "stable 30 minutes" (runtime) — dilewati.
- Phase 3: no clicks / no pops (butuh audio device + telinga).
- Phase 4: MIDI device & footswitch fisik.

## Log keputusan / catatan
- 2026-06-06 Scope = Phase 2-4; git = branch per phase; eksekusi jalan terus; verifikasi tanpa audio hardware.
- 2026-06-06 Baseline build diperbaiki (reconfigure setelah project dipindah path). Roadmap docs + Makefile fix di-commit ke main (cb87ca3).
