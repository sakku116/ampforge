package com.ampforge.controller

import java.io.ByteArrayOutputStream

/**
 * Reassembles complete SysEx messages (F0..F7) from arbitrary MIDI byte chunks.
 * Bluetooth MIDI can split a single SysEx across several characteristic writes, so the
 * consumer of this collector only ever sees whole, well-formed Controller MIDI
 * Protocol messages. Non-SysEx bytes outside the framing are ignored.
 */
class SysExCollector(private val onMessage: (ByteArray) -> Unit) {

    private val buffer = ByteArrayOutputStream()
    private var inSysEx = false

    fun push(data: ByteArray) {
        for (b in data) {
            val u = b.toInt() and 0xFF
            when {
                u == 0xF0 -> {
                    buffer.reset()
                    buffer.write(u)
                    inSysEx = true
                }
                u == 0xF7 -> if (inSysEx) {
                    buffer.write(u)
                    inSysEx = false
                    onMessage(buffer.toByteArray())
                }
                inSysEx -> buffer.write(u)
            }
        }
    }

    fun reset() {
        buffer.reset()
        inSysEx = false
    }
}
