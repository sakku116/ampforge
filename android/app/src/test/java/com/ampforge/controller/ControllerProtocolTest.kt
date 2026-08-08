package com.ampforge.controller

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Deterministic tests for the Kotlin port of the Controller MIDI Protocol contract. */
class ControllerProtocolTest {

    private fun bytesOf(vararg b: Int) = ByteArray(b.size) { b[it].toByte() }

    private fun descriptor(index: Int, flags: Int, label: String): ByteArray {
        val labelBytes = label.toByteArray(Charsets.US_ASCII)
        return bytesOf(index, flags, labelBytes.size) + labelBytes
    }

    @Test
    fun `ready message is F0 7D 10 01 01 00 F7`() {
        assertArrayEquals(bytesOf(0xF0, 0x7D, 0x10, 0x01, 0x01, 0x00, 0xF7), ControllerProtocol.readyMessage())
    }

    @Test
    fun `note messages use channel 16`() {
        assertArrayEquals(bytesOf(0x9F, 60, 127), ControllerProtocol.noteMessage(60, 127))
        assertArrayEquals(bytesOf(0x9F, 67, 0), ControllerProtocol.noteMessage(67, 0))
    }

    @Test
    fun `parses a complete snapshot with mixed descriptors`() {
        val descriptors = mutableListOf<ByteArray>()
        val expected = mutableListOf<ControllerProtocol.ButtonDescriptor>()
        // Unassigned button 0, active stomp 1, bypassed stomp 2, active preset 3,
        // inactive preset 4, muted stomp 5, long label 6, empty label 7.
        descriptors += descriptor(0, 0x00, "")
        expected += ControllerProtocol.unassigned(0)
        descriptors += descriptor(1, 0x01 or 0x04, "OD Drive")
        expected += ControllerProtocol.ButtonDescriptor(1, true, false, true, false, false, "OD Drive")
        descriptors += descriptor(2, 0x01 or 0x08, "Delay")
        expected += ControllerProtocol.ButtonDescriptor(2, true, false, false, true, false, "Delay")
        descriptors += descriptor(3, 0x01 or 0x02 or 0x04, "Lead")
        expected += ControllerProtocol.ButtonDescriptor(3, true, true, true, false, false, "Lead")
        descriptors += descriptor(4, 0x01 or 0x02 or 0x08, "Clean")
        expected += ControllerProtocol.ButtonDescriptor(4, true, true, false, true, false, "Clean")
        descriptors += descriptor(5, 0x01 or 0x10, "Wah")
        expected += ControllerProtocol.ButtonDescriptor(5, true, false, false, false, true, "Wah")
        descriptors += descriptor(6, 0x01 or 0x04, "A very long stomp box name")
        expected += ControllerProtocol.ButtonDescriptor(6, true, false, true, false, false, "A very long stomp box name")
        descriptors += descriptor(7, 0x01 or 0x02, "")
        expected += ControllerProtocol.ButtonDescriptor(7, true, true, false, false, false, "")

        val body = bytesOf(0x7D, 0x10, 0x10, 0x01, 0x00) + descriptors.reduce { a, b -> a + b }
        val message = bytesOf(0xF0) + body + bytesOf(0xF7)

        val parsed = ControllerProtocol.parse(message) as ControllerProtocol.HostMessage.Snapshot
        assertEquals(1, parsed.major)
        assertEquals(0, parsed.minor)
        assertEquals(expected, parsed.buttons)
    }

    @Test
    fun `accepts a snapshot without SysEx framing`() {
        val body = bytesOf(0x7D, 0x10, 0x10, 0x01, 0x00) +
            List(8) { descriptor(it, 0x00, "") }.reduce { a, b -> a + b }
        assertNotNull(ControllerProtocol.parse(body) as ControllerProtocol.HostMessage.Snapshot)
    }

    @Test
    fun `rejects a truncated snapshot`() {
        val body = bytesOf(0x7D, 0x10, 0x10, 0x01, 0x00) +
            List(7) { descriptor(it, 0x00, "") }.reduce { a, b -> a + b } // only 7 of 8
        assertNull(ControllerProtocol.parse(bytesOf(0xF0) + body + bytesOf(0xF7)))
    }

    @Test
    fun `rejects a snapshot with trailing garbage`() {
        val body = bytesOf(0x7D, 0x10, 0x10, 0x01, 0x00) +
            List(8) { descriptor(it, 0x00, "") }.reduce { a, b -> a + b } + bytesOf(0x99)
        assertNull(ControllerProtocol.parse(bytesOf(0xF0) + body + bytesOf(0xF7)))
    }

    @Test
    fun `rejects a descriptor with an oversized label length`() {
        val body = bytesOf(0x7D, 0x10, 0x10, 0x01, 0x00) +
            descriptor(0, 0x00, "") +
            bytesOf(1, 0x01, ControllerProtocol.MAX_LABEL_BYTES + 1, 0x41) +
            List(6) { descriptor(it + 2, 0x00, "") }.reduce { a, b -> a + b }
        assertNull(ControllerProtocol.parse(bytesOf(0xF0) + body + bytesOf(0xF7)))
    }

    @Test
    fun `parses an incremental update`() {
        val message = bytesOf(0xF0, 0x7D, 0x10, 0x11, 0x02, 0x01 or 0x08, 0x05) + "Delay".toByteArray() + bytesOf(0xF7)
        val update = ControllerProtocol.parse(message) as ControllerProtocol.HostMessage.Update
        assertEquals(ControllerProtocol.ButtonDescriptor(2, true, false, false, true, false, "Delay"), update.button)
    }

    @Test
    fun `rejects a truncated update`() {
        val message = bytesOf(0xF0, 0x7D, 0x10, 0x11, 0x02, 0x01 or 0x08, 0x05, 0x44) // label cut short
        assertNull(ControllerProtocol.parse(message))
    }

    @Test
    fun `parses a protocol mismatch`() {
        val message = bytesOf(0xF0, 0x7D, 0x10, 0x12, 0x02, 0x01, 0xF7)
        val mismatch = ControllerProtocol.parse(message) as ControllerProtocol.HostMessage.Mismatch
        assertEquals(2, mismatch.major)
        assertEquals(1, mismatch.minor)
    }

    @Test
    fun `rejects a malformed mismatch`() {
        assertNull(ControllerProtocol.parse(bytesOf(0xF0, 0x7D, 0x10, 0x12, 0x02, 0xF7))) // missing minor
    }

    @Test
    fun `rejects foreign sysEx and non-sysEx bytes`() {
        assertNull(ControllerProtocol.parse(bytesOf(0xF0, 0x00, 0x20, 0x29, 0x10, 0x01, 0x00, 0xF7))) // other manufacturer
        assertNull(ControllerProtocol.parse(bytesOf(0xF0, 0x7D, 0x99, 0x01, 0x01, 0x00, 0xF7)))       // wrong device id
        assertNull(ControllerProtocol.parse(bytesOf(0x90, 60, 127)))                                 // a note message
        assertNull(ControllerProtocol.parse(byteArrayOf()))                                          // empty
    }

    private fun assertNotNull(value: Any?) {
        if (value == null) throw AssertionError("expected non-null")
    }
}
