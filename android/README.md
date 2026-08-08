# Amp Forge Controller — Native Android Controller App (#12)

A small Kotlin Android app that turns an Android phone into the Bluetooth MIDI
Controller for Amp Forge. It is a **BLE MIDI peripheral**: it advertises the standard
MIDI-over-BLE GATT service so Windows pairs it as a regular MIDI input+output device,
then communicates with the host over the versioned Controller MIDI Protocol
(`src/ControllerProtocol.h`).

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
- **Controller Performance Mode**: landscape-locked, display kept awake, BLE
  advertising and control traffic only while foregrounded; inactive while
  backgrounded or locked, recovering with a fresh snapshot on return.

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

On Android 12+ the app requests `BLUETOOTH_CONNECT` and `BLUETOOTH_ADVERTISE` on first
launch; allow both.

## Pairing with Windows

1. Open the app on the phone (it starts advertising while foregrounded).
2. On Windows: Settings → Bluetooth & devices → Add device → Bluetooth, pick the phone.
   Windows registers it as a MIDI input **and** output.
3. Start Amp Forge; the footer shows `Controller: Connected` once the host receives the
   app's `READY`. The phone shows the full mirror after the host's `SNAPSHOT`.

When the phone's screen is locked or the app is backgrounded the controller goes
inactive; returning to the foreground re-advertises and triggers a fresh snapshot.

## Architecture

| File | Responsibility |
|---|---|
| `MainActivity.kt` | Controller Performance Mode, permissions, lifecycle, status, message handling |
| `ControllerSurfaceView.kt` | The 4×2 surface and the Controller Visual State rendering |
| `BleMidiServer.kt` | BLE GATT server + advertising; packet send/receive (MMA MIDI over BLE) |
| `BleMidiPacket.kt` | BLE MIDI packet header encoding/decoding (timestamp + message index) |
| `ControllerProtocol.kt` | Kotlin port of the wire contract in `src/ControllerProtocol.h` |
| `SysExCollector.kt` | Reassembles `F0..F7` SysEx split across BLE writes |

The controller→host direction uses the same BLE MIDI packet format Windows expects
(spec-compliant), so the host side needs no changes. End-to-end pairing/reconnect,
latency, and disconnect behavior are validated on the Personal Device Target (see
issue #13).
