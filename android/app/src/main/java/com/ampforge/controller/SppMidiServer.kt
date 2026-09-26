package com.ampforge.controller

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothServerSocket
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Amp Forge Controller's classic Bluetooth (SPP/RFCOMM) MIDI transport.
 *
 * The phone is an SPP server: Windows pairs it over classic Bluetooth and exposes the
 * service as a COM port ("Standard Serial over Bluetooth link (COMx)"), which Amp Forge
 * opens and exchanges raw MIDI bytes over. This replaces the BLE GATT transport —
 * Windows 11 24H2+ removed the BLE MIDI driver (bthmidi.sys) and Windows MIDI Services
 * on those builds has no BLE transport yet, so classic SPP is the transport that works
 * on every Windows build without extra software.
 *
 * The wire protocol (ControllerProtocol SysEx) and note traffic are unchanged: the
 * stream carries raw MIDI bytes and SysExCollector reassembles the F0..F7 framing.
 */
class SppMidiServer(
    context: Context,
    private val onMidiData: (ByteArray) -> Unit,
    private val onConnectionChange: (connected: Boolean) -> Unit,
) {
    companion object {
        /** Standard Serial Port Profile UUID — Windows binds COM ports only to services
         *  whose class list includes 0x1101; a custom UUID stays invisible to it. */
        val SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        const val SERVICE_NAME = "Amp Forge Controller"
        private const val READ_BUFFER = 1024
    }

    private val appContext = context.applicationContext
    private val adapter =
        (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
    private val mainHandler = Handler(Looper.getMainLooper())

    private var serverSocket: BluetoothServerSocket? = null
    private var socket: BluetoothSocket? = null
    private var acceptThread: Thread? = null
    private var readThread: Thread? = null
    private var connected = false
    private val started = AtomicBoolean(false)

    val isConnected: Boolean get() = connected

    /** Starts listening (and re-listening after each host disconnects). Idempotent. */
    @SuppressLint("MissingPermission")
    fun start() {
        val bt = adapter ?: return
        if (!bt.isEnabled) return
        // Re-create the listener whenever it is gone: the socket dies when Bluetooth is
        // toggled off, and the latched started-flag must not block a fresh listen.
        if (serverSocket != null) return
        val server = try {
            bt.listenUsingRfcommWithServiceRecord(SERVICE_NAME, SPP_UUID)
        } catch (e: Exception) {
            android.util.Log.e("ControllerSPP", "listen failed", e)
            return
        }
        serverSocket = server
        started.set(true)
        acceptThread = Thread({ acceptLoop(server) }, "controller-spp-accept").also { it.start() }
    }

    /** Stops listening and closes any connection (inactive while backgrounded/locked). */
    @SuppressLint("MissingPermission")
    fun stop() {
        if (!started.getAndSet(false)) return
        connected = false
        try { serverSocket?.close() } catch (_: Exception) {}
        try { socket?.close() } catch (_: Exception) {}
        serverSocket = null
        socket = null
        acceptThread = null
        readThread = null
    }

    /** Sends one raw MIDI message (note or READY SysEx) to the connected host. */
    @SuppressLint("MissingPermission")
    fun send(message: ByteArray) {
        val s = socket ?: return
        val out: OutputStream = try { s.outputStream } catch (e: Exception) { return }
        try {
            out.write(message)
            out.flush()
        } catch (e: Exception) {
            disconnect(s)
        }
    }

    private fun acceptLoop(server: BluetoothServerSocket) {
        while (started.get()) {
            val s = try {
                server.accept()
            } catch (e: Exception) {
                if (serverSocket === server) serverSocket = null // socket died — allow re-listen
                return
            }
            if (!started.get()) {
                try { s.close() } catch (_: Exception) {}
                return
            }
            handleConnection(s)
        }
    }

    private fun handleConnection(s: BluetoothSocket) {
        // One controller at a time: close any previous connection first.
        try { socket?.close() } catch (_: Exception) {}
        socket = s
        connected = true
        mainHandler.post { onConnectionChange(true) }
        readThread = Thread({ readLoop(s) }, "controller-spp-read").also { it.start() }
    }

    private fun readLoop(s: BluetoothSocket) {
        val input: InputStream = try { s.inputStream } catch (e: Exception) { disconnect(s); return }
        val buf = ByteArray(READ_BUFFER)
        while (started.get() && s === socket) {
            val n = try { input.read(buf) } catch (e: Exception) { -1 }
            if (n <= 0) {
                disconnect(s)
                return
            }
            onMidiData(buf.copyOf(n))
        }
    }

    private fun disconnect(s: BluetoothSocket) {
        if (s !== socket) return
        connected = false
        try { s.close() } catch (_: Exception) {}
        if (s === socket) socket = null
        mainHandler.post { onConnectionChange(false) }
        // acceptLoop keeps listening for the next host connection.
    }
}
