# Amp Forge Controller — Native Android Controller App (#12)

A small Kotlin Android app that turns an Android phone into the Bluetooth MIDI
Controller for Amp Forge. It is a **classic Bluetooth (SPP/RFCOMM) server**: Windows
pairs it over classic Bluetooth and exposes the service as a COM port, which Amp Forge
opens and exchanges raw MIDI bytes over, using the versioned Controller MIDI Protocol
(`src/ControllerProtocol.h`).

> **Why SPP instead of BLE MIDI:** Windows 11 24H2+ removed the BLE MIDI driver
> (`bthmidi.sys`) and Windows MIDI Services on those builds has no BLE transport yet, so
> a BLE MIDI peripheral can no longer pair as a MIDI device on current Windows. Classic
> SPP is exposed as a standard COM port on every Windows build, with no extra software.

This is the "Controller App Module" — it lives in this repository so host and protocol
changes stay atomic.

## What it does

- **Controller Surface**: a landscape 4×2 grid of eight buttons emitting fixed notes
  60–67 on MIDI channel 16 (Controller Note Set, in reading order).
- **Controller Mirror**: renders the host-owned mirror — label, Stomp/Preset type, and
  every Controller Visual State (blue active Stomp, amber bypassed Stomp, teal active
  Preset, dim inactive Preset, muted section-bypassed, neutral unassigned).
- **Synchronization**: sends `READY` on foreground/reconnect; applies full `SNAPSHOT`
  and incremental `UPDATE` feedback; shows a visible `MISMATCH` state for incompatible
  host versions.
- **Controller Performance Mode**: landscape-locked, display kept awake; the SPP
  listener stays alive while the process lives (a re-listen could change the RFCOMM
  channel Windows caches at pairing time), but control traffic is sent only while
  foregrounded — backgrounded/locked is inactive, recovering with a fresh snapshot on
  return.

No assignment editor: Amp Forge owns every Controller Assignment via Controller Learn.

## Build

Requirements: JDK 17+ and the Android SDK (platform 35, build-tools 35). The Gradle
wrapper downloads Gradle itself.

```bash
# point Gradle at your SDK (or set ANDROID_HOME)
echo "sdk.dir=C:/path/to/Android/Sdk" > local.properties

./gradlew :app:assembleRelease      # signed APK
./gradlew :app:testDebugUnitTest    # deterministic protocol/transport JVM tests
```

Output: `app/build/outputs/apk/release/app-release.apk`.

The release APK is signed with `keystore/controller-release.jks` (alias
`ampforge-controller`, password `ampforge`) — a committed keystore is fine for a
sideloaded personal app and keeps the signature stable across updates. Do not use it
for anything distributed publicly.

Install on the phone:

```bash
adb install -r app/build/outputs/apk/release/app-release.apk
```

On Android 12+ the app requests `BLUETOOTH_CONNECT` and `BLUETOOTH_ADVERTISE`
(discoverability) on first launch; allow both.

## Pairing with Windows (classic Bluetooth SPP)

1. Open the app on the phone (it starts listening for SPP connections).
2. On Windows: Settings → Bluetooth & devices → Add device → Bluetooth — **leave the
   pairing dialog open** so the PC is discoverable.
3. On the phone: Settings → Bluetooth → pick the PC (e.g. `E75PA2H`) → Pair. Pairing
   from the phone side avoids Windows' PIN prompt. The SPP listener stays alive while
   the app is paused, so the pairing dialog cannot kill the service record.
4. Windows exposes the service as a COM port ("Standard Serial over Bluetooth link
   (COMx)"). Start Amp Forge; its `SerialMidi` module opens the port, the phone sends
   `READY`, and the footer shows `Controller: Connected` after the host replies with a
   `SNAPSHOT`.

Known limitation: Windows caches the RFCOMM channel at pairing time. If the app's
process is restarted (crash, update, aggressive OS kill) the channel can change, and a
Windows re-pair is needed to refresh it.

## Architecture

| File | Responsibility |
|---|---|
| `MainActivity.kt` | Controller Performance Mode, permissions, lifecycle, status, message handling |
| `ControllerSurfaceView.kt` | The 4×2 surface and the Controller Visual State rendering |
| `SppMidiServer.kt` | Classic Bluetooth SPP (RFCOMM) server; raw MIDI byte stream in/out |
| `ControllerProtocol.kt` | Kotlin port of the wire contract in `src/ControllerProtocol.h` |
| `SysExCollector.kt` | Reassembles `F0..F7` SysEx split across stream chunks |

The wire contract is unchanged from the BLE design — the stream carries the same raw
MIDI bytes, so the host-side protocol layer (`ControllerBridge`) is untouched. Host
integration lives in `src/SerialMidi.*`. End-to-end pairing/reconnect, latency, and
disconnect behavior are validated on the Personal Device Target (see issue #13).
