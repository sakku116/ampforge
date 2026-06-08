# Guitar VST3 Host — Makefile
# Build & run targets (tested with Git Bash on Windows)

.PHONY: help build release run clean rebuild scan-worker logs check-sdks install-asio install-vst2

BUILD_DIR := build
CONFIG ?= Debug
CMAKE ?= $(shell if command -v cmake >/dev/null 2>&1; then command -v cmake; elif [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then printf '%s\n' "/c/Program Files/CMake/bin/cmake.exe"; else printf '%s\n' cmake; fi)
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
	@echo "  make check-sdks     — Check if ASIO and VST2 SDKs are present"
	@echo "  make install-asio   — Download and install ASIO SDK into asio/"
	@echo "  make install-vst2   — Install VST2 SDK (guided manual step)"
	@echo ""
	@echo "Environment:"
	@echo "  CONFIG=Debug|Release — Build config (default: Debug)"
	@echo ""

build:
	@echo "[*] Building $(CONFIG)..."
	@test -f $(BUILD_DIR)/CMakeCache.txt || "$(CMAKE)" -S . -B $(BUILD_DIR)
	@"$(CMAKE)" --build $(BUILD_DIR) --config $(CONFIG)
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

ASIO_DIR  := asio
VST2_DIR  := vst2sdk
ASIO_HEADER := $(ASIO_DIR)/common/asiosdk2.h
VST2_HEADER := $(VST2_DIR)/pluginterfaces/vst2.x/aeffect.h

check-sdks:
	@echo "=== SDK Status ==="
	@if [ -f "$(ASIO_HEADER)" ]; then \
		echo "[+] ASIO SDK   : found ($(ASIO_DIR)/) — JUCE_ASIO=1 will be enabled"; \
	else \
		echo "[-] ASIO SDK   : NOT found — run 'make install-asio'"; \
	fi
	@if [ -f "$(VST2_HEADER)" ]; then \
		echo "[+] VST2 SDK   : found ($(VST2_DIR)/) — JUCE_PLUGINHOST_VST=1 will be enabled"; \
	else \
		echo "[-] VST2 SDK   : NOT found — run 'make install-vst2'"; \
	fi

install-asio:
	@if [ -f "$(ASIO_HEADER)" ]; then \
		echo "[+] ASIO SDK already installed at $(ASIO_DIR)/"; \
	else \
		echo "[*] Cloning ASIO SDK..."; \
		git clone https://github.com/audiosdk/asio.git $(ASIO_DIR) && \
		rm -f $(BUILD_DIR)/CMakeCache.txt && \
		echo "[+] ASIO SDK installed at $(ASIO_DIR)/ — CMake cache cleared, run 'make build' to rebuild"; \
	fi

install-vst2:
	@if [ -f "$(VST2_HEADER)" ]; then \
		echo "[+] VST2 SDK already installed at $(VST2_DIR)/"; \
	else \
		echo "[*] Cloning VST2 SDK (steinbergmedia mirror)..."; \
		git clone https://github.com/R-Tur/VST_SDK_2.4.git _vst2_tmp && \
		mkdir -p "$(VST2_DIR)/pluginterfaces/vst2.x" && \
		cp _vst2_tmp/pluginterfaces/vst2.x/aeffect.h  "$(VST2_DIR)/pluginterfaces/vst2.x/" && \
		cp _vst2_tmp/pluginterfaces/vst2.x/aeffectx.h "$(VST2_DIR)/pluginterfaces/vst2.x/" && \
		cp _vst2_tmp/pluginterfaces/vst2.x/vstfxstore.h "$(VST2_DIR)/pluginterfaces/vst2.x/" 2>/dev/null || true && \
		rm -rf _vst2_tmp && \
		rm -f $(BUILD_DIR)/CMakeCache.txt && \
		echo "[+] VST2 headers installed at $(VST2_DIR)/ — CMake cache cleared, run 'make build' to rebuild"; \
	fi

.DEFAULT_GOAL := help
