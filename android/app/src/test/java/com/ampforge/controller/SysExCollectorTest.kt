package com.ampforge.controller

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Deterministic tests for SysEx reassembly across BLE MIDI chunks. */
class SysExCollectorTest {

    private val messages = mutableListOf<ByteArray>()

    private fun collector() = SysExCollector { messages += it }

    @Test
    fun `a complete message in one chunk is delivered once`() {
        val c = collector()
        c.push(ControllerProtocol.readyMessage())
        assertEquals(1, messages.size)
        assertTrue(messages[0].contentEquals(ControllerProtocol.readyMessage()))
    }

    @Test
    fun `a message split across chunks is reassembled`() {
        val c = collector()
        val message = ControllerProtocol.readyMessage() // F0 7D 10 01 01 00 F7
        c.push(message.copyOfRange(0, 3))
        c.push(message.copyOfRange(3, 6))
        c.push(message.copyOfRange(6, 7))
        assertEquals(1, messages.size)
        assertTrue(messages[0].contentEquals(message))
    }

    @Test
    fun `junk outside the SysEx framing is ignored`() {
        val c = collector()
        c.push(byteArrayOf(0x90.toByte(), 60, 127)) // a note before any F0
        c.push(byteArrayOf(0xF0.toByte(), 0x7D))
        c.push(byteArrayOf(0x11.toByte(), 0xF7.toByte(), 0x99.toByte(), 0x00)) // junk after F7
        assertEquals(1, messages.size)
        assertTrue(messages[0].contentEquals(byteArrayOf(0xF0.toByte(), 0x7D, 0x11, 0xF7.toByte())))
    }

    @Test
    fun `two messages back to back are delivered in order`() {
        val c = collector()
        val a = ControllerProtocol.readyMessage()
        val b = byteArrayOf(0xF0.toByte(), 0x7D, 0x10, 0x12, 0x02, 0x01, 0xF7.toByte())
        c.push(a + b)
        assertEquals(2, messages.size)
        assertTrue(messages[0].contentEquals(a))
        assertTrue(messages[1].contentEquals(b))
    }

    @Test
    fun `an unterminated message is held until F7`() {
        val c = collector()
        c.push(byteArrayOf(0xF0.toByte(), 0x7D, 0x10, 0x10))
        assertEquals(0, messages.size)
        c.push(byteArrayOf(0xF7.toByte()))
        assertEquals(1, messages.size)
        assertTrue(messages[0].contentEquals(byteArrayOf(0xF0.toByte(), 0x7D, 0x10, 0x10, 0xF7.toByte())))
    }
}
