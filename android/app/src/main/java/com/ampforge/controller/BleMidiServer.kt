package com.ampforge.controller

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattServer
import android.bluetooth.BluetoothGattServerCallback
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The Native Controller App's Bluetooth MIDI peripheral: a BLE GATT server that
 * advertises the standard MIDI service so Windows pairs it as a MIDI device.
 *
 * - Host → app: characteristic writes arrive as BLE MIDI packets; their headers are
 *   stripped and the raw MIDI bytes are forwarded to [onMidiData].
 * - App → host: [send] packetizes a MIDI message and delivers it as a notification on
 *   the MIDI Data characteristic (only after the central enables notifications).
 *
 * The server is only started while the app is foregrounded (Controller Performance
 * Mode); [stop] makes the controller inactive while backgrounded or locked. Windows
 * owns pairing; the app re-advertises and reconnects automatically while foregrounded.
 */
class BleMidiServer(
    context: Context,
    private val onMidiData: (ByteArray) -> Unit,
    private val onConnectionChange: (connected: Boolean) -> Unit,
) {
    companion object {
        /** MMA "MIDI over Bluetooth LE" service and characteristics. */
        val MIDI_SERVICE = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
        val MIDI_DATA = UUID.fromString("7772E5DB-3868-4112-A1A9-F2669D106BF3")
        val MIDI_CONFIG = UUID.fromString("6088A7E2-7383-41A6-9C09-7C8A5A6CCF2E")
        val CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        /** Default MIDI Configuration value (MIDI protocol 1.0, no limits). */
        val DEFAULT_MIDI_CONFIG = byteArrayOf(0x64, 0x00, 0x00, 0x00)
    }

    private val appContext = context.applicationContext
    private val bluetoothManager = appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val adapter = bluetoothManager?.adapter
    private val mainHandler = Handler(Looper.getMainLooper())

    private var gattServer: BluetoothGattServer? = null
    private var advertiser: BluetoothLeAdvertiser? = null
    private var advertising = false
    private var midiDataCharacteristic: BluetoothGattCharacteristic? = null
    private var connectedDevice: BluetoothDevice? = null
    private var notifyEnabled = false
    private var pendingNotification: ByteArray? = null
    private val started = AtomicBoolean(false)

    private val gattCallback = object : BluetoothGattServerCallback() {
        override fun onConnectionStateChange(device: BluetoothDevice, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connectedDevice = device
                notifyEnabled = false
                post { onConnectionChange(true) }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED && device == connectedDevice) {
                connectedDevice = null
                notifyEnabled = false
                pendingNotification = null
                post { onConnectionChange(false) }
            }
        }

        override fun onCharacteristicWriteRequest(
            device: BluetoothDevice,
            requestId: Int,
            characteristic: BluetoothGattCharacteristic,
            preparedWrite: Boolean,
            responseNeeded: Boolean,
            offset: Int,
            value: ByteArray,
        ) {
            when (characteristic.uuid) {
                MIDI_DATA -> onMidiData(BleMidiPacket.decode(value)) // one packet per write
                MIDI_CONFIG -> Unit // configuration accepted, no action needed
                else -> return respond(device, requestId, responseNeeded, BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED)
            }
            respond(device, requestId, responseNeeded, BluetoothGatt.GATT_SUCCESS)
        }

        override fun onDescriptorWriteRequest(
            device: BluetoothDevice,
            requestId: Int,
            descriptor: BluetoothGattDescriptor,
            preparedWrite: Boolean,
            responseNeeded: Boolean,
            offset: Int,
            value: ByteArray,
        ) {
            if (descriptor.uuid == CCCD && value.isNotEmpty() && (value[0].toInt() and 0x01) != 0) {
                notifyEnabled = true
                val queued = pendingNotification
                pendingNotification = null
                if (queued != null) sendLocked(queued)
            }
            respond(device, requestId, responseNeeded, BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicReadRequest(
            device: BluetoothDevice,
            requestId: Int,
            offset: Int,
            characteristic: BluetoothGattCharacteristic,
        ) {
            if (characteristic.uuid == MIDI_CONFIG) {
                gattServer?.sendResponse(device, requestId, BluetoothGatt.GATT_SUCCESS, offset, DEFAULT_MIDI_CONFIG)
            } else {
                gattServer?.sendResponse(device, requestId, BluetoothGatt.GATT_REQUEST_NOT_SUPPORTED, offset, null)
            }
        }
    }

    private fun respond(device: BluetoothDevice, requestId: Int, responseNeeded: Boolean, status: Int) {
        if (responseNeeded) gattServer?.sendResponse(device, requestId, status, 0, null)
    }

    private val advertiseCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings) { advertising = true }
        override fun onStartFailure(errorCode: Int) { advertising = false }
    }

    /** Starts advertising and serving the MIDI GATT service. Safe to call repeatedly. */
    @SuppressLint("MissingPermission")
    fun start() {
        if (started.getAndSet(true)) return
        val bt = adapter ?: return
        if (!bt.isEnabled) return
        val server = bluetoothManager?.openGattServer(appContext, gattCallback) ?: return

        val data = BluetoothGattCharacteristic(
            MIDI_DATA,
            BluetoothGattCharacteristic.PROPERTY_WRITE or
                BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE or
                BluetoothGattCharacteristic.PROPERTY_NOTIFY,
            BluetoothGattCharacteristic.PERMISSION_WRITE,
        )
        data.addDescriptor(
            BluetoothGattDescriptor(CCCD, BluetoothGattDescriptor.PERMISSION_READ or BluetoothGattDescriptor.PERMISSION_WRITE),
        )
        val config = BluetoothGattCharacteristic(
            MIDI_CONFIG,
            BluetoothGattCharacteristic.PROPERTY_READ or BluetoothGattCharacteristic.PROPERTY_WRITE,
            BluetoothGattCharacteristic.PERMISSION_READ or BluetoothGattCharacteristic.PERMISSION_WRITE,
        )
        val service = BluetoothGattService(MIDI_SERVICE, BluetoothGattService.SERVICE_TYPE_PRIMARY)
        service.addCharacteristic(data)
        service.addCharacteristic(config)
        server.addService(service)

        gattServer = server
        midiDataCharacteristic = data

        val leAdvertiser = bt.bluetoothLeAdvertiser
        advertiser = leAdvertiser
        if (leAdvertiser != null) {
            val settings = AdvertiseSettings.Builder()
                .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
                .setConnectable(true)
                .build()
            val adData = AdvertiseData.Builder().addServiceUuid(ParcelUuid(MIDI_SERVICE)).build()
            leAdvertiser.startAdvertising(settings, adData, advertiseCallback)
        }
    }

    /** Stops advertising and closes the GATT server (inactive while backgrounded/locked). */
    @SuppressLint("MissingPermission")
    fun stop() {
        if (!started.getAndSet(false)) return
        advertiser?.stopAdvertising(advertiseCallback)
        advertiser = null
        gattServer?.close()
        gattServer = null
        midiDataCharacteristic = null
        connectedDevice = null
        notifyEnabled = false
        pendingNotification = null
        advertising = false
    }

    val isAdvertising: Boolean get() = advertising
    val isConnected: Boolean get() = connectedDevice != null

    /**
     * Sends a MIDI message (e.g. a note or the READY SysEx) to the connected host.
     * Queued until the central enables notifications so the READY is never lost.
     */
    @SuppressLint("MissingPermission")
    fun send(message: ByteArray) {
        if (message.isEmpty()) return
        synchronized(this) {
            if (!notifyEnabled || connectedDevice == null) {
                pendingNotification = message
                return
            }
            sendLocked(message)
        }
    }

    private fun sendLocked(message: ByteArray) {
        val server = gattServer ?: return
        val device = connectedDevice ?: return
        val characteristic = midiDataCharacteristic ?: return
        val timestamp = (SystemClock.elapsedRealtime() and 0x3FFF).toInt()
        for (packet in BleMidiPacket.encode(message, timestamp)) {
            server.notifyCharacteristicChanged(device, characteristic, true, packet)
        }
    }

    private fun post(action: () -> Unit) { mainHandler.post(action) }
}
