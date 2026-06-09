# Guitar VST3 Host — Makefile
# Build & run targets (tested with Git Bash on Windows)

.PHONY: help build release run run-release clean rebuild scan-worker logs check-sdks

BUILD_DIR := build
CONFIG ?= Debug
CMAKE   ?= $(shell if command -v cmake >/dev/null 2>&1; then command -v cmake; elif [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then printf '%s\n' "/c/Program Files/CMake/bin/cmake.exe"; else printf '%s\n' cmake; fi)
VS_GEN  ?= Visual Studio 18 2026
EXE_PATH := $(BUILD_DIR)/GtrFxSim_artefacts/$(CONFIG)/Guitar VST3 Host.exe
WORKER_EXE := $(BUILD_DIR)/GtrFxSim_artefacts/$(CONFIG)/GuitarVST3ScanWorker.exe
LOG_FILE := ~/AppData/Roaming/GtrFxSim/host.log

help:
	@echo "Guitar VST3 Host — Makefile targets:"
	@echo ""
	@echo "  make build          — Build Debug (default)"
	@echo "  make release        — Build Release"
	@echo "  make run            — Run Debug build"
	@echo "  make run-release    — Run Release build"
	@echo "  make rebuild        — Clean + build Debug"
	@echo "  make clean          — Remove build/ folder"
	@echo "  make scan-worker    — Verify scan worker exists"
	@echo "  make logs           — Show last 50 lines of host.log"
	@echo "  make check-sdks     — Show ASIO and VST2 SDK status"
	@echo ""
	@echo "Environment:"
	@echo "  CONFIG=Debug|Release — Build config (default: Debug)"
	@echo "  VS_GEN=<generator>   — CMake generator (default: Visual Studio 18 2026)"
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
	@echo "[*] Running Guitar VST3 Host ($(CONFIG))..."
	@"$(EXE_PATH)"

run-release: release scan-worker
	@echo "[*] Running Guitar VST3 Host (Release)..."
	@"$(BUILD_DIR)/GtrFxSim_artefacts/Release/Guitar VST3 Host.exe"

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

.DEFAULT_GOAL := help
