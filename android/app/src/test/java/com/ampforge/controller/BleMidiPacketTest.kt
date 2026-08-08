package com.ampforge.controller

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Deterministic tests for the BLE MIDI packet framing (MMA spec). */
class BleMidiPacketTest {

    @Test
    fun `header encodes the timestamp MSBs and the message index`() {
        // timestamp 256 -> bits 12..7 = 0b000010 -> header = 0b00001000 | index
        assertEquals(0x08.toByte(), BleMidiPacket.header(256, 0))
        assertEquals(0x0B.toByte(), BleMidiPacket.header(256, 3))
        assertEquals(0, BleMidiPacket.indexOf(BleMidiPacket.header(100, 0)))
        assertEquals(3, BleMidiPacket.indexOf(BleMidiPacket.header(100, 3)))
    }

    @Test
    fun `a short message fits in one packet`() {
        val ready = ControllerProtocol.readyMessage() // 8 bytes
        val packets = BleMidiPacket.encode(ready, timestamp = 42)
        assertEquals(1, packets.size)
        assertEquals(0, BleMidiPacket.indexOf(packets[0][0]))
        assertTrue(packets[0].contentEquals(byteArrayOf(BleMidiPacket.header(42, 0)) + ready))
    }

    @Test
    fun `a long message is split and reassembles losslessly`() {
        val message = ByteArray(45) { (it + 1).toByte() } // 45 bytes -> 3 packets at payload 20
        val packets = BleMidiPacket.encode(message, timestamp = 7, payload = 20)
        assertEquals(3, packets.size)
        assertEquals(0, BleMidiPacket.indexOf(packets[0][0]))
        assertEquals(0, BleMidiPacket.indexOf(packets[1][0])) // 20 mod 4 = 0
        assertEquals(0, BleMidiPacket.indexOf(packets[2][0]))

        val reassembled = packets.flatMap { BleMidiPacket.decode(it).toList() }.toByteArray()
        assertTrue(reassembled.contentEquals(message))
    }

    @Test
    fun `index increments for non-multiple payload sizes`() {
        val message = ByteArray(9) { 0x41 } // 9 bytes -> 2 packets at payload 5
        val packets = BleMidiPacket.encode(message, timestamp = 0, payload = 5)
        assertEquals(2, packets.size)
        assertEquals(0, BleMidiPacket.indexOf(packets[0][0]))
        assertEquals(1, BleMidiPacket.indexOf(packets[1][0])) // 5 mod 4 = 1
    }
}
