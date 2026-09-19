package com.ampforge.controller

/**
 * Kotlin port of the Controller MIDI Protocol (#11) — the same wire contract as
 * [src/ControllerProtocol.h](https://github.com/sakku116/ampforge/blob/dev/src/ControllerProtocol.h).
 * Every data byte is 7-bit safe.
 *
 * Message shape: F0 7D 10 <cmd> <payload...> F7
 *   - 0x01 READY    controller → host (requested protocol version); host replies with a
 *                   SNAPSHOT when compatible, or a MISMATCH otherwise.
 *   - 0x10 SNAPSHOT host → controller: host version + eight Button descriptors.
 *   - 0x11 UPDATE   host → controller: one Button descriptor (incremental feedback).
 *   - 0x12 MISMATCH host → controller: host version (no synchronization).
 *
 * Button descriptor: <index:1> <flags:1> <len:1> <label:len bytes (ASCII, 7-bit safe)>.
 * Flags: bit0 assigned, bit1 isPreset, bit2 active (stomp on / preset selected),
 *        bit3 bypassed (stomp off / preset inactive), bit4 sectionBypassed (muted).
 */
object ControllerProtocol {

    const val MANUFACTURER_ID = 0x7D
    const val DEVICE_ID = 0x10
    const val CMD_READY = 0x01
    const val CMD_SNAPSHOT = 0x10
    const val CMD_UPDATE = 0x11
    const val CMD_MISMATCH = 0x12

    const val PROTOCOL_MAJOR = 1
    const val PROTOCOL_MINOR = 0

    /** Controller Surface: eight fixed notes on the Android MIDI Channel, in reading order. */
    const val NOTE_SET_START = 60
    const val NUM_BUTTONS = 8
    const val MIDI_CHANNEL = 16 // 1-based; reserved for the Android controller
    const val MAX_LABEL_BYTES = 40

    private const val FLAG_ASSIGNED = 0x01
    private const val FLAG_IS_PRESET = 0x02
    private const val FLAG_ACTIVE = 0x04
    private const val FLAG_BYPASSED = 0x08
    private const val FLAG_SECTION_BYPASSED = 0x10

    /** One Controller Surface button as seen by the phone. */
    data class ButtonDescriptor(
        val index: Int,
        val assigned: Boolean,
        val isPreset: Boolean,
        val active: Boolean,
        val bypassed: Boolean,
        val sectionBypassed: Boolean,
        val label: String,
    )

    fun unassigned(index: Int) = ButtonDescriptor(index, false, false, false, false, false, "")

    /** A decoded host→controller message. */
    sealed class HostMessage {
        /** Complete Controller Mirror (cmd 0x10). */
        data class Snapshot(val major: Int, val minor: Int, val buttons: List<ButtonDescriptor>) : HostMessage()

        /** Incremental feedback for one button (cmd 0x11). */
        data class Update(val button: ButtonDescriptor) : HostMessage()

        /** Protocol version mismatch (cmd 0x12): the host refuses to synchronize. */
        data class Mismatch(val major: Int, val minor: Int) : HostMessage()
    }

    /** READY request: F0 7D 10 01 <major> <minor> F7. Sent on foreground/reconnect. */
    fun readyMessage(): ByteArray = byteArrayOf(
        0xF0.toByte(), MANUFACTURER_ID.toByte(), DEVICE_ID.toByte(), CMD_READY.toByte(),
        PROTOCOL_MAJOR.toByte(), PROTOCOL_MINOR.toByte(), 0xF7.toByte(),
    )

    /** Note on/off on the Android MIDI Channel: 0x9F <note> <velocity>. */
    fun noteMessage(note: Int, velocity: Int): ByteArray = byteArrayOf(
        (0x90 or (MIDI_CHANNEL - 1)).toByte(), note.toByte(), velocity.toByte(),
    )

    /**
     * Decodes a complete SysEx message (F0..F7 framing optional) into a [HostMessage].
     * Returns null when the message is not a well-formed Controller MIDI Protocol
     * message — the reader of the host contract, mirroring the C++ isSnapshot /
     * isButtonUpdate / isProtocolMismatch helpers.
     */
    fun parse(message: ByteArray): HostMessage? {
        val body = stripFraming(message)
        if (body == null || body.size < 3) return null
        if (body[0] != MANUFACTURER_ID.toByte() || body[1] != DEVICE_ID.toByte()) return null
        return when (body[2].toInt() and 0xFF) {
            CMD_SNAPSHOT -> parseSnapshotBody(body)
            CMD_UPDATE -> parseUpdateBody(body)
            CMD_MISMATCH -> parseMismatchBody(body)
            else -> null
        }
    }

    private fun stripFraming(message: ByteArray): ByteArray? {
        var start = 0
        var end = message.size
        if (end > 0 && (message[0].toInt() and 0xFF) == 0xF0) start = 1
        if (end > start && (message[end - 1].toInt() and 0xFF) == 0xF7) end -= 1
        if (end < start) return null
        return message.copyOfRange(start, end)
    }

    private fun parseSnapshotBody(body: ByteArray): HostMessage.Snapshot? {
        if (body.size < 5) return null
        var pos = 5
        val buttons = ArrayList<ButtonDescriptor>(NUM_BUTTONS)
        repeat(NUM_BUTTONS) {
            val parsed = parseDescriptor(body, pos) ?: return null
            buttons.add(parsed.first)
            pos = parsed.second
        }
        if (pos != body.size) return null // trailing garbage after the last descriptor
        return HostMessage.Snapshot(body[3].toInt() and 0xFF, body[4].toInt() and 0xFF, buttons)
    }

    private fun parseUpdateBody(body: ByteArray): HostMessage.Update? {
        var pos = 3
        val parsed = parseDescriptor(body, pos) ?: return null
        pos = parsed.second
        if (pos != body.size) return null
        return HostMessage.Update(parsed.first)
    }

    private fun parseMismatchBody(body: ByteArray): HostMessage.Mismatch? {
        if (body.size != 5) return null
        return HostMessage.Mismatch(body[3].toInt() and 0xFF, body[4].toInt() and 0xFF)
    }

    private fun parseDescriptor(body: ByteArray, pos: Int): Pair<ButtonDescriptor, Int>? {
        if (pos + 3 > body.size) return null
        var p = pos
        val index = body[p++].toInt() and 0xFF
        val flags = body[p++].toInt() and 0xFF
        val len = body[p++].toInt() and 0xFF
        if (len > MAX_LABEL_BYTES || p + len > body.size) return null
        val label = StringBuilder(len)
        repeat(len) { label.append((body[p++].toInt() and 0xFF).toChar()) }
        val descriptor = ButtonDescriptor(
            index = index,
            assigned = flags and FLAG_ASSIGNED != 0,
            isPreset = flags and FLAG_IS_PRESET != 0,
            active = flags and FLAG_ACTIVE != 0,
            bypassed = flags and FLAG_BYPASSED != 0,
            sectionBypassed = flags and FLAG_SECTION_BYPASSED != 0,
            label = label.toString(),
        )
        return descriptor to p
    }
}
