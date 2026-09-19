package com.ampforge.controller

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager

/**
 * Amp Forge Controller — the Native Android Controller App (#12).
 *
 * Runs in Controller Performance Mode: landscape-locked, display kept awake, BLE MIDI
 * advertising + the Controller Surface active only while foregrounded. On resume or
 * reconnect it sends a READY and the host answers with a Controller Snapshot; UPDATE
 * messages and MISMATCH responses are rendered as they arrive.
 */
class MainActivity : Activity() {

    private lateinit var surfaceView: ControllerSurfaceView
    private var bleServer: BleMidiServer? = null
    private var bleConnected = false

    private val collector = SysExCollector { message ->
        runOnUiThread { handleHostMessage(message) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        surfaceView = ControllerSurfaceView(this) { index, down -> sendNote(index, down) }
        surfaceView.setOnApplyWindowInsetsListener { v, insets ->
            v.setPadding(0, insets.systemWindowInsetTop, 0, insets.systemWindowInsetBottom)
            insets
        }
        setContentView(surfaceView)
        hideSystemBars()

        bleServer = BleMidiServer(
            this,
            onMidiData = { chunk -> collector.push(chunk) },
            onConnectionChange = { connected ->
                bleConnected = connected
                if (connected) {
                    surfaceView.statusConnected = true
                    surfaceView.statusText = "Connected"
                    sendReady()
                } else {
                    surfaceView.statusConnected = false
                    surfaceView.statusText = "Disconnected — waiting for host"
                }
            },
        )
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (Build.VERSION.SDK_INT >= 31 && hasMissingBluetoothPermissions()) {
            surfaceView.statusText = "Bluetooth permission required"
            surfaceView.statusConnected = false
            requestPermissions(REQUIRED_BLUETOOTH_PERMISSIONS, REQ_BLUETOOTH_PERMISSIONS)
            return
        }
        startController()
    }

    override fun onPause() {
        super.onPause()
        // Inactive Controller App: no advertising, no control traffic while backgrounded/locked.
        bleServer?.stop()
        bleConnected = false
        surfaceView.statusConnected = false
        surfaceView.statusText = "Paused — inactive"
    }

    override fun onDestroy() {
        bleServer?.stop()
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_BLUETOOTH_PERMISSIONS) {
            if (!hasMissingBluetoothPermissions()) {
                startController()
            } else {
                surfaceView.statusText = "Bluetooth permission required"
            }
        }
    }

    private fun startController() {
        bleServer?.start()
        surfaceView.statusText = if (bleServer?.isAdvertising == true) "Advertising…" else "Waiting for Bluetooth"
    }

    private fun sendReady() {
        bleServer?.send(ControllerProtocol.readyMessage())
    }

    private fun sendNote(index: Int, down: Boolean) {
        // Notes are sent only while the BLE link is up; the host owns the action.
        if (!bleConnected) return
        val note = ControllerProtocol.NOTE_SET_START + index
        bleServer?.send(ControllerProtocol.noteMessage(note, if (down) 127 else 0))
    }

    /** Applies a decoded host message to the Controller Mirror (message thread). */
    private fun handleHostMessage(message: ByteArray) {
        when (val host = ControllerProtocol.parse(message)) {
            is ControllerProtocol.HostMessage.Snapshot -> {
                surfaceView.setMirror(host.buttons)
                surfaceView.statusMismatch = false
                surfaceView.statusConnected = true
                surfaceView.statusText = "Connected — v${host.major}.${host.minor}"
            }
            is ControllerProtocol.HostMessage.Update -> surfaceView.updateButton(host.button)
            is ControllerProtocol.HostMessage.Mismatch -> {
                surfaceView.statusMismatch = true
                surfaceView.statusConnected = false
                surfaceView.statusText = "Incompatible host v${host.major}.${host.minor} — update Amp Forge"
            }
            null -> Unit // unrelated SysEx — ignore
        }
    }

    private fun hasMissingBluetoothPermissions(): Boolean =
        REQUIRED_BLUETOOTH_PERMISSIONS.any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }

    private fun hideSystemBars() {
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    }

    companion object {
        private const val REQ_BLUETOOTH_PERMISSIONS = 1001

        /** Android 12+ runtime permissions for the GATT server and BLE advertising. */
        private val REQUIRED_BLUETOOTH_PERMISSIONS = arrayOf(
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_ADVERTISE,
        )
    }
}
