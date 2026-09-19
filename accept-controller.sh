#!/usr/bin/env bash
# Amp Forge — Android Controller Target-Device Acceptance (issue #13)
# Walks the human through the end-to-end acceptance scenarios only they can perform
# (phone, Windows pairing, Amp Forge UI) and records the evidence on issue #13.
# Transport: classic Bluetooth SPP (phone = RFCOMM server, Windows = COM port).
# Usage: bash accept-controller.sh   (Git Bash on Windows)
set -euo pipefail

EXE="build/AmpForge_artefacts/Debug/Amp Forge.exe"
APK="android/app/build/outputs/apk/release/app-release.apk"
LOG="$HOME/AppData/Roaming/AmpForge/host.log"

step() { printf '\n=== %s ===\n' "$1"; }
ok()   { printf '  [OK] %s\n' "$1"; }
fail() { printf '  [!!] %s\n' "$1"; }
ask()  { # ask "question"  -> echoes "y"/"n"
  local a
  while true; do
    read -r -p "  $1 (y/n) " a || exit 1
    case "$a" in y|Y) echo y; return;; n|N) echo n; return;; esac
  done
}
capture() { # capture "question" -> sets REPLY
  read -r -p "  $1 " REPLY || REPLY=""
}

echo "Amp Forge — Android Controller Target-Device Acceptance (#13)"
echo "Transport: classic Bluetooth SPP. The phone is an RFCOMM server; Windows"
echo "exposes it as a COM port; Amp Forge opens the port and speaks the Controller"
echo "MIDI Protocol (notes 60-67 on channel 16, F0 7D 10 SysEx)."
echo "You need: the phone with the Controller App installed, this host build, and"
echo "the release APK: $APK"
read -r -p "Press Enter when ready, or Ctrl-C to quit... " _ || exit 0

PC_NAME=""
while [ -z "$PC_NAME" ]; do
  capture "Windows PC name as shown on the phone's Bluetooth list (e.g. E75PA2H):"
  PC_NAME="$REPLY"
done
COM_PORT=""
while [ -z "$COM_PORT" ]; do
  capture "COM port Windows assigned (Device Manager > Ports, e.g. COM5):"
  COM_PORT="$REPLY"
done

# ── Stage 1: install + first launch ─────────────────────────────────────────
step "1/9  Install and launch the Controller App"
echo "  - Install: adb install -r $APK (or sideload)."
echo "  - Launch 'Amp Forge Controller'; allow BOTH permission dialogs"
echo "    (Bluetooth permission, then discoverability)."
echo "  - The app must show 'Listening…'."
if [ "$(ask "App shows 'Listening…'?")" = y ]; then ok "app listening"; LISTEN_OK=y
else fail "not listening"; LISTEN_OK=n; fi

# ── Stage 2: classic pairing (from the phone) ───────────────────────────────
step "2/9  Pair over classic Bluetooth"
echo "  - Windows: Settings > Bluetooth & devices > Add device > Bluetooth — leave"
echo "    the dialog OPEN (this makes the PC discoverable)."
echo "  - Phone: Settings > Bluetooth > tap '$PC_NAME' > Pair/Confirm (pair from the"
echo "    phone side to avoid the Windows PIN prompt)."
echo "  - Windows exposes the SPP service as a COM port ($COM_PORT expected)."
if [ "$(ask "Pairing succeeded and the COM port exists?")" = y ]; then ok "paired + COM port"; PAIR_OK=y
else fail "pairing/COM port failed"; PAIR_OK=n; fi

# ── Stage 3: host connects and synchronizes ─────────────────────────────────
step "3/9  Start Amp Forge; verify Controller Connection"
echo "  - Start Amp Forge ($EXE). If an audio warning appears, click Retry."
echo "  - Expect in host footer: 'Controller: Connected'."
echo "  - Expect on phone: 'Connected — v1.0' and the 8-button mirror."
if [ "$(ask "Both statuses correct?")" = y ]; then ok "READY -> SNAPSHOT verified"; SYNC_OK=y
else fail "no sync"; SYNC_OK=n; fi

# ── Stage 4: reconnect re-synchronizes the mirror ───────────────────────────
step "4/9  Reconnect while the host chain changed"
echo "  - While connected, in Amp Forge rename a learned slot (mirror label changes"
echo "    on the phone immediately — record in stage 5)."
echo "  - Background the app on the phone (Home), wait ~5s, reopen it."
echo "  - Expect phone 'Connected — v1.0' again and a fresh SNAPSHOT matching the host."
if [ "$(ask "After reconnect the mirror matched the current host state?")" = y ]; then ok "resync verified"; RESYNC_OK=y
else fail "mirror stale after reconnect"; RESYNC_OK=n; fi

# ── Stage 5: Controller Mirror follows host changes ─────────────────────────
step "5/9  Mirror updates across host actions"
echo "  Prereq: learn several buttons (notes 60-67, channel 16) to Stomp/Preset slots"
echo "  via right-click > 'Learn Control'. Unlearned buttons stay empty."
echo "  For EACH, check the phone shows the change immediately:"
echo "    a) Learn a new button  -> it appears (assigned) with the slot label"
echo "    b) Rename a learned slot -> label changes on the phone"
echo "    c) Add / remove / duplicate / reorder a slot; drag across sections"
echo "    d) Toggle a stomp badge in the host -> button toggles on the phone"
echo "    e) Activate a preset slot -> preset button highlights (active)"
echo "    f) Toggle SECTION bypass -> affected buttons show muted state"
echo "    g) Recall a template -> mirror refreshes to it"
FAILED_MIRROR=""
capture "Which of a-g failed, if any (comma list, or Enter for none):"
FAILED_MIRROR="$REPLY"
if [ -z "$FAILED_MIRROR" ]; then ok "all mirror triggers verified"; MIRROR_OK=y
else fail "mirror triggers failing: $FAILED_MIRROR"; MIRROR_OK=n; fi

# ── Stage 6: coexistence with other MIDI devices ────────────────────────────
step "6/9  Other MIDI devices keep working"
echo "  - Keep a second MIDI input active (keyboard, DAW, or another phone with a"
echo "    BT MIDI app)."
echo "  - Its own learned actions must still execute."
echo "  - Press a controller button (60-67 on ch 16): must still work."
if [ "$(ask "Both the other device's actions AND the controller buttons worked?")" = y ]; then ok "coexistence verified"; COEXIST_OK=y
else fail "coexistence broken"; COEXIST_OK=n; fi

# ── Stage 7: foreground/background + protocol incompatibility ───────────────
step "7/9  Backgrounding and Protocol Incompatibility"
echo "  - With the app connected, background it (Home) or lock the phone."
echo "  - The app sends no control traffic while backgrounded; host keeps working."
echo "  - Bring the app back: it re-sends READY; expect a fresh SNAPSHOT."
if [ "$(ask "Background = inactive, foreground = reconnected with fresh snapshot?")" = y ]; then ok "foreground/background verified"; FG_OK=y
else fail "foreground/background broken"; FG_OK=n; fi
echo "  Protocol Incompatibility (optional, needs a rebuild):"
echo "    - In android/.../ControllerProtocol.kt set PROTOCOL_MAJOR = 2, rebuild,"
echo "      install, connect. Expect phone: 'Incompatible host v1.0 — update Amp Forge';"
echo "      host footer: 'Controller: v Mismatch'; NO mirror sync. Revert and rebuild."
echo "    - Covered by JVM tests otherwise; device check is optional."
MISMATCH_NOTE="not exercised on device (covered by JVM tests)"
if [ "$(ask "Did you run the optional device mismatch check?")" = y ]; then
  if [ "$(ask "Phone showed 'Incompatible host' AND host showed 'v Mismatch' AND no mirror sync?")" = y ]; then
    ok "protocol incompatibility verified on device"; MISMATCH_OK=y
    MISMATCH_NOTE="verified on device: phone 'Incompatible host v1.0', host 'Controller: v Mismatch', no mirror sync"
  else fail "mismatch behavior wrong"; MISMATCH_OK=n; MISMATCH_NOTE="device check ran but behavior did not match spec"; fi
fi

# ── Stage 8: responsiveness under 50 ms ─────────────────────────────────────
step "8/9  Measure button-to-feedback latency"
echo "  Method: record a 240 fps slow-motion video (phone camera or screen recorder)"
echo "  of pressing a controller button and the host badge changing (or the phone"
echo "  button state toggling after the host UPDATE)."
echo "  Latency ms = frames_between_press_and_feedback / 240 * 1000."
FRAMES=""
while [ -z "$FRAMES" ]; do
  capture "Frames between press and visible feedback (240 fps):"
  FRAMES="$REPLY"
done
LATENCY_MS=$(awk "BEGIN{printf \"%.1f\", $FRAMES/240*1000}")
if awk "BEGIN{exit !($LATENCY_MS < 50)}"; then ok "latency $LATENCY_MS ms < 50 ms"; LAT_OK=y
else fail "latency $LATENCY_MS ms >= 50 ms"; LAT_OK=n; fi
LAT_NOTE=""
capture "Measurement notes (device, camera method, trial count; Enter for none):"
LAT_NOTE="$REPLY"

# ── Stage 9: record evidence ────────────────────────────────────────────────
step "9/9  Record evidence on issue #13"
EXTRA=""
capture "Other findings/defects/limitations (Enter for none):"
EXTRA="$REPLY"

COMMENT="## Android Controller Target-Device Acceptance — Result (SPP transport)

**Device:** paired to Windows PC \`$PC_NAME\` over classic Bluetooth (SPP)
**COM port:** $COM_PORT
**Host build:** $(git rev-parse --short HEAD 2>/dev/null || echo n/a)

### 1. Pairing / COM port / connection
App listening: ${LISTEN_OK:-?}; paired + COM port: ${PAIR_OK:-?}
Host 'Controller: Connected' + phone 'Connected — v1.0' (READY -> SNAPSHOT): ${SYNC_OK:-?}

### 2. Reconnect re-sync
Background -> foreground re-sends READY and refreshes the mirror: ${RESYNC_OK:-?}

### 3. Controller Mirror triggers (a-g)
All mirror triggers verified: ${MIRROR_OK:-?}${FAILED_MIRROR:+ (failing: $FAILED_MIRROR)}

### 4. Coexistence with other MIDI devices
Other device actions + controller buttons both worked on ch 16: ${COEXIST_OK:-?}

### 5. Foreground/background + Protocol Incompatibility
Background = inactive, foreground = reconnected: ${FG_OK:-?}
Protocol Incompatibility: $MISMATCH_NOTE

### 6. Responsiveness
$LATENCY_MS ms (measured from $FRAMES frames at 240 fps) — under 50 ms: ${LAT_OK:-?}${LAT_NOTE:+ ($LAT_NOTE)}

### 7. Findings
${EXTRA:-None.}
"

echo
echo "──────────────────────────────────────────────────"
echo "Issue #13 comment body:"
echo "──────────────────────────────────────────────────"
echo "$COMMENT"
if [ "$(ask "Post this evidence to issue #13?")" = y ]; then
  gh issue comment 13 --body "$COMMENT"
  ok "comment posted"
  if [ "$(ask "Close issue #13?")" = y ]; then
    gh issue close 13 --comment "Acceptance complete on the Personal Device Target."
    ok "issue closed"
  fi
else
  echo "Not posted. Re-run to regenerate; answers are not persisted."
fi
echo "Done."
