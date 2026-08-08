package com.ampforge.controller

/**
 * BLE MIDI transport (MMA "MIDI over Bluetooth LE"): MIDI bytes travel in packets of
 * up to (MTU - 3) bytes, each prefixed with a header byte:
 *
 *   bits 7..2 = 6 most significant bits of the 13-bit timestamp (1 ms units)
 *   bits 1..0 = message index — the byte position (mod 4) of the first MIDI byte in
 *               this packet within its message
 *
 * Windows (the host side of the Controller Connection) implements this spec exactly,
 * so both directions must encode and decode these headers. The Native Controller App's
 * own traffic (notes, the READY SysEx) always fits in a single packet.
 */
object BleMidiPacket {

    /** Largest payload per packet when no MTU was negotiated (default 23-byte MTU). */
    const val DEFAULT_PAYLOAD = 20

    fun header(timestamp: Int, index: Int): Byte =
        ((((timestamp ushr 7) and 0x3F) shl 2) or (index and 0x03)).toByte()

    fun indexOf(header: Byte): Int = header.toInt() and 0x03

    /** Splits one MIDI message into BLE MIDI packets for a given payload size. */
    fun encode(message: ByteArray, timestamp: Int, payload: Int = DEFAULT_PAYLOAD): List<ByteArray> {
        val size = payload.coerceIn(1, DEFAULT_PAYLOAD)
        val out = ArrayList<ByteArray>()
        var pos = 0
        var index = 0
        while (pos < message.size) {
            val n = minOf(size, message.size - pos)
            val packet = ByteArray(n + 1)
            packet[0] = header(timestamp, index)
            System.arraycopy(message, pos, packet, 1, n)
            out.add(packet)
            pos += n
            index = (index + n) % 4
        }
        return out
    }

    /** Strips the BLE MIDI header from one received packet and returns the MIDI bytes. */
    fun decode(packet: ByteArray): ByteArray =
        if (packet.size <= 1) ByteArray(0) else packet.copyOfRange(1, packet.size)
}
