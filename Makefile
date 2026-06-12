# Amp Forge — Makefile
# Build & run targets (tested with Git Bash on Windows)

.PHONY: help build release run run-release clean rebuild scan-worker logs check-sdks installer portable

BUILD_DIR := build
CONFIG ?= Debug
VERSION  := $(shell grep -m1 ' VERSION [0-9]' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
CMAKE   ?= $(shell if command -v cmake >/dev/null 2>&1; then command -v cmake; elif [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then printf '%s\n' "/c/Program Files/CMake/bin/cmake.exe"; else printf '%s\n' cmake; fi)
ISCC    ?= $(shell \
	if [ -x "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" ]; then \
		printf '%s\n' '"/c/Program Files (x86)/Inno Setup 6/ISCC.exe"'; \
	elif [ -x "/c/Program Files/Inno Setup 6/ISCC.exe" ]; then \
		printf '%s\n' '"/c/Program Files/Inno Setup 6/ISCC.exe"'; \
	elif command -v ISCC >/dev/null 2>&1; then \
		command -v ISCC; \
	else \
		printf '%s\n' ISCC; \
	fi)
VS_GEN  ?= Visual Studio 18 2026
EXE_PATH := $(BUILD_DIR)/AmpForge_artefacts/$(CONFIG)/Amp Forge.exe
WORKER_EXE := $(BUILD_DIR)/AmpForge_artefacts/$(CONFIG)/AmpForgeScanWorker.exe
LOG_FILE := ~/AppData/Roaming/AmpForge/host.log
INSTALLER_SCRIPT := installer/AmpForge.iss
INSTALLER_OUT    := dist/AmpForge-Setup-$(VERSION)-x64.exe
PORTABLE_OUT     := dist/AmpForge-$(VERSION)-portable-x64.zip

help:
	@echo "Amp Forge — Makefile targets:"
	@echo ""
	@echo "  make build          — Build Debug (default)"
	@echo "  make release        — Build Release"
	@echo "  make run            — Run Debug build"
	@echo "  make run-release    — Run Release build"
	@echo "  make rebuild        — Clean + build Debug"
	@echo "  make clean          — Remove build/ folder"
	@echo "  make portable       — Build Release + zip portable package → dist/"
	@echo "  make installer      — Build Release + package installer (requires Inno Setup 6)"
	@echo "  make scan-worker    — Verify scan worker exists"
	@echo "  make logs           — Show last 50 lines of host.log"
	@echo "  make check-sdks     — Show ASIO and VST2 SDK status"
	@echo ""
	@echo "Environment:"
	@echo "  CONFIG=Debug|Release — Build config (default: Debug)"
	@echo "  VS_GEN=<generator>   — CMake generator (default: Visual Studio 18 2026)"
	@echo "  ISCC=<path>          — Path to ISCC.exe (auto-detected)"
	@echo ""

build:
	@echo "[*] Building $(CONFIG)..."
	@test -f $(BUILD_DIR)/CMakeCache.txt || "$(CMAKE)" -S . -B $(BUILD_DIR) -G "$(VS_GEN)" -A x64
	@"$(CMAKE)" --build $(BUILD_DIR) --config $(CONFIG) --parallel
	@echo "[+] Build complete: $(EXE_PATH)"

release:
	@$(MAKE) build CONFIG=Release

rebuild: clean build
	@echo "[+] Rebuild complete"

run: build scan-worker
	@echo "[*] Running Amp Forge ($(CONFIG))..."
	@"$(EXE_PATH)"

run-release: release scan-worker
	@echo "[*] Running Amp Forge (Release)..."
	@"$(BUILD_DIR)/AmpForge_artefacts/Release/Amp Forge.exe"

clean:
	@echo "[*] Removing build folder..."
	@rm -rf $(BUILD_DIR)
	@echo "[+] Clean complete"

scan-worker:
	@test -f "$(WORKER_EXE)" || { echo "[!] Scan worker missing: $(WORKER_EXE)"; exit 1; }

logs:
	@if [ -f "$(LOG_FILE)" ]; then \
		tail -50 "$(LOG_FILE)"; \
	else \
		echo "[!] Log file not found: $(LOG_FILE)"; \
	fi

ASIO_DIR    := ASIOSDK
VST2_DIR    := vst2sdk
ASIO_HEADER := $(ASIO_DIR)/common/asio.h
VST2_HEADER := $(VST2_DIR)/pluginterfaces/vst2.x/aeffect.h

check-sdks:
	@echo "=== SDK Status ==="
	@if [ -f "$(ASIO_HEADER)" ]; then \
		echo "[+] ASIO SDK : $(ASIO_DIR)/ — GPL v3 (bundled)"; \
	else \
		echo "[-] ASIO SDK : NOT found — ASIOSDK/ folder missing from repo clone"; \
	fi
	@if [ -f "$(VST2_HEADER)" ]; then \
		echo "[+] VST2 SDK : $(VST2_DIR)/ — MIT clean-room (bundled)"; \
	else \
		echo "[-] VST2 SDK : NOT found — vst2sdk/ folder missing from repo clone"; \
	fi

installer: release scan-worker
	@echo "[*] Building installer v$(VERSION)..."
	@mkdir -p dist
	@$(ISCC) "/DMyAppVersion=$(VERSION)" "$(INSTALLER_SCRIPT)"
	@echo "[+] Installer: $(INSTALLER_OUT)"

portable: release scan-worker
	@echo "[*] Packaging portable zip v$(VERSION)..."
	@rm -rf dist/portable-staging
	@mkdir -p dist/portable-staging
	@cp "$(BUILD_DIR)/AmpForge_artefacts/Release/Amp Forge.exe" dist/portable-staging/
	@cp "$(BUILD_DIR)/AmpForge_artefacts/Release/AmpForgeScanWorker.exe" dist/portable-staging/
	@touch dist/portable-staging/portable.txt
	@cd dist/portable-staging && powershell -NoProfile -Command "Compress-Archive -Path * -DestinationPath '../AmpForge-$(VERSION)-portable-x64.zip' -Force"
	@rm -rf dist/portable-staging
	@echo "[+] Portable: $(PORTABLE_OUT)"

.DEFAULT_GOAL := help
