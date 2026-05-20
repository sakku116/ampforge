# Guitar VST3 Host — Makefile
# Build & run targets (tested with Git Bash on Windows)

.PHONY: help build release run clean rebuild scan-worker logs

BUILD_DIR := build
CONFIG ?= Debug
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
	@echo ""
	@echo "Environment:"
	@echo "  CONFIG=Debug|Release — Build config (default: Debug)"
	@echo ""

build:
	@echo "[*] Building $(CONFIG)..."
	@test -d $(BUILD_DIR) || cmake -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR) --config $(CONFIG)
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

.DEFAULT_GOAL := help
