# Amp Forge — Release Guide

Panduan manual untuk melakukan release baru tanpa bantuan AI.

---

## Persiapan (sekali saja)

1. Buat branch `dev` dari `main`:
   ```bash
   git checkout -b dev
   git push -u origin dev
   ```

2. Protect branch `main` di GitHub:
   **Settings → Branches → Add rule → Branch name: `main` → Require pull request before merging**

---

## Setiap Kali Rilis

### 1. Tentukan versi baru (Semantic Versioning)

| Jenis perubahan | Bump | Contoh |
|-----------------|------|--------|
| Fitur baru, backward-compatible | MINOR | `0.1.0 → 0.2.0` |
| Bug fix / patch kecil | PATCH | `0.1.0 → 0.1.1` |
| Breaking change / redesign besar | MAJOR | `0.1.0 → 1.0.0` |

### 2. Pastikan di branch `dev` dan up-to-date

```bash
git checkout dev
git pull
```

### 3. Bump versi — satu baris di `CMakeLists.txt`

Buka `CMakeLists.txt`, ubah baris 2:

```cmake
project(AmpForge VERSION 0.2.0 LANGUAGES CXX)
```

> Versi ini otomatis muncul di: title bar app, nama file zip portable, nama file installer.

### 4. Update `CHANGELOG.md`

Lihat semua commit sejak tag terakhir:

```bash
git log v0.1.0..HEAD --oneline --no-merges
```

Tambahkan entry baru di **paling atas** `CHANGELOG.md`:

```markdown
## [0.2.0] — YYYY-MM-DD

### Added
- Deskripsi fitur baru yang user lihat

### Fixed
- Deskripsi bug yang diperbaiki

### Changed
- Deskripsi perubahan behavior yang sudah ada
```

> Tulis dalam present tense ("Add", "Fix", bukan "Added", "Fixed").
> Hanya tulis perubahan yang user-visible; skip refactor internal.

### 5. Commit dan push ke `dev`

```bash
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: release v0.2.0"
git push origin dev
```

### 6. Buat Pull Request `dev → main`

Buka GitHub → **Pull requests → New pull request**
- Base: `main`
- Compare: `dev`
- Title: `Release v0.2.0`

Atau via CLI (jika `gh` sudah terinstall):

```bash
gh pr create --base main --head dev --title "Release v0.2.0" --notes-file CHANGELOG.md
```

Tunggu review, lalu merge.

### 7. Tag dan sync setelah PR merged

```bash
git checkout main
git pull

git tag v0.2.0 -m "Release v0.2.0"
git push origin v0.2.0

git checkout dev
git merge main
git push origin dev
```

### 8. Build portable zip

```bash
make portable
# Output: dist/AmpForge-0.2.0-portable-x64.zip
```

> Butuh Git Bash. Jika dari PowerShell, jalankan via: `bash -c "make portable"`

### 9. Buat GitHub Release

```bash
gh release create v0.2.0 "dist/AmpForge-0.2.0-portable-x64.zip" \
  --title "Amp Forge v0.2.0" \
  --notes-file CHANGELOG.md
```

Atau manual di GitHub: **Releases → Draft a new release → pilih tag `v0.2.0` → upload zip → Publish**.

---

## Quick Reference

```bash
# Versi saat ini
grep -m1 'VERSION [0-9]' CMakeLists.txt

# Tag terakhir
git describe --tags --abbrev=0

# Commit sejak tag terakhir
git log $(git describe --tags --abbrev=0)..HEAD --oneline --no-merges

# Build portable
make portable
```

---

## Struktur Branch

```
main        — protected, hanya terima PR; semua commit di sini adalah release
  ↑ PR
dev         — integrasi semua fitur; tempat bump versi + tulis changelog
  ↑ PR
feature/*   — satu fitur/fix per branch, cut dari dev
fix/*
```
