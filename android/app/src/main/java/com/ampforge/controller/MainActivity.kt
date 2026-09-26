package com.ampforge.controller

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager

/**
 * Amp Forge Controller — the Native Android Controller App (#12).
 *
 * Runs in Controller Performance Mode: landscape-locked, display kept awake, classic
 * Bluetooth SPP (RFCOMM) server. Windows pairs it and exposes the service as a COM port;
 * on resume or reconnect the app sends a READY and the host answers with a Controller
 * Snapshot; UPDATE messages and MISMATCH responses are rendered as they arrive. The
 * listener stays alive while the process lives (a re-listen could change the RFCOMM
 * channel Windows caches); control traffic is sent only while foregrounded.
 */
class MainActivity : Activity() {

    private lateinit var surfaceView: ControllerSurfaceView
    private var sppServer: SppMidiServer? = null
    private var connected = false

    private val prefs by lazy { getSharedPreferences("controller", Context.MODE_PRIVATE) }
    private var discoverabilityRequested = false
    private var foregrounded = false

    /** Re-starts/stops the SPP listener when Bluetooth itself is toggled off and on. */
    private val btStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)) {
                BluetoothAdapter.STATE_ON -> sppServer?.start()
                BluetoothAdapter.STATE_OFF -> sppServer?.stop()
            }
        }
    }

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

        sppServer = SppMidiServer(
            this,
            onMidiData = { chunk -> collector.push(chunk) },
            onConnectionChange = { isConnected ->
                connected = isConnected
                if (isConnected) {
                    prefs.edit().putBoolean("host_paired_once", true).apply()
                    surfaceView.statusConnected = true
                    surfaceView.statusText = "Connected"
                    // READY only while foregrounded: a backgrounded app must stay inactive.
                    if (foregrounded) sendReady()
                } else {
                    surfaceView.statusConnected = false
                    surfaceView.statusText = "Disconnected — waiting for host"
                }
            },
        )
        registerReceiver(
            btStateReceiver,
            IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED),
        )
    }

    override fun onResume() {
        super.onResume()
        foregrounded = true
        hideSystemBars()
        if (Build.VERSION.SDK_INT >= 31 && hasMissingBluetoothPermissions()) {
            surfaceView.statusText = "Bluetooth permission required"
            surfaceView.statusConnected = false
            requestPermissions(REQUIRED_BLUETOOTH_PERMISSIONS, REQ_BLUETOOTH_PERMISSIONS)
            return
        }
        startController()
        // On resume re-sync with the host: READY -> fresh Controller Snapshot.
        if (connected) {
            surfaceView.statusConnected = true
            surfaceView.statusText = "Connected"
            sendReady()
        }
    }

    override fun onPause() {
        super.onPause()
        foregrounded = false
        // Inactive Controller App: no control traffic while backgrounded/locked. The SPP
        // server itself stays alive (stable RFCOMM channel — Windows caches the SDP
        // channel at pairing time, so re-creating the listener would break the link until
        // a re-pair); the connection persists but nothing is sent until resume.
        surfaceView.statusConnected = false
        surfaceView.statusText = "Paused — inactive"
    }

    override fun onDestroy() {
        sppServer?.stop()
        try { unregisterReceiver(btStateReceiver) } catch (_: Exception) {}
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
        sppServer?.start()
        surfaceView.statusText = if (sppServer?.isConnected == true) "Connected" else "Listening…"
        requestDiscoverabilityIfNeeded()
    }

    /**
     * Classic pairing needs the phone discoverable; Windows connects through the COM
     * port afterwards, so discoverability is only requested until the first host link.
     */
    private fun requestDiscoverabilityIfNeeded() {
        if (discoverabilityRequested) return   // once per session — never loop the dialog
        discoverabilityRequested = true
        if (prefs.getBoolean("host_paired_once", false)) return
        val adapter = (getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter ?: return
        if (!adapter.isEnabled) return
        try {
            startActivityForResult(
                Intent(BluetoothAdapter.ACTION_REQUEST_DISCOVERABLE).putExtra(
                    BluetoothAdapter.EXTRA_DISCOVERABLE_DURATION, 300),
                REQ_DISCOVERABLE,
            )
        } catch (e: Exception) {
            // No activity handles the request on this device — the user can still open
            // Bluetooth settings to make the phone discoverable.
        }
    }

    private fun sendReady() {
        sppServer?.send(ControllerProtocol.readyMessage())
    }

    private fun sendNote(index: Int, down: Boolean) {
        // Notes are sent only while the link is up AND the app is foregrounded.
        if (!connected || !foregrounded) return
        val note = ControllerProtocol.NOTE_SET_START + index
        sppServer?.send(ControllerProtocol.noteMessage(note, if (down) 127 else 0))
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
        private const val REQ_DISCOVERABLE = 1002

        /** Android 12+ runtime permissions: SPP server socket + classic discoverability. */
        private val REQUIRED_BLUETOOTH_PERMISSIONS = arrayOf(
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_ADVERTISE,
        )
    }
}
