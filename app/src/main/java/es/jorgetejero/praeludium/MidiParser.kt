package es.jorgetejero.praeludium

import java.io.ByteArrayOutputStream

/** Convierte el chorro de bytes que manda el órgano en mensajes MIDI completos. */
class MidiParser(private val emit: (ByteArray, Long) -> Unit) {
    private var status = 0
    private var expected = 0
    private val buf = ByteArray(2)
    private var count = 0
    private var inSysex = false
    private val sysex = ByteArrayOutputStream()

    fun reset() {
        status = 0
        expected = 0
        count = 0
        inSysex = false
        sysex.reset()
    }

    fun feed(data: ByteArray, offset: Int, len: Int, t: Long) {
        for (i in offset until offset + len) {
            val b = data[i].toInt() and 0xFF
            when {
                b >= 0xF8 -> { /* reloj, active sensing, start/stop: se ignoran */ }
                b == 0xF0 -> {
                    inSysex = true
                    sysex.reset()
                    sysex.write(0xF0)
                }
                b == 0xF7 -> {
                    if (inSysex) {
                        sysex.write(0xF7)
                        emit(sysex.toByteArray(), t)
                        inSysex = false
                    }
                }
                b >= 0x80 -> {
                    inSysex = false
                    count = 0
                    if (b >= 0xF0) {
                        status = 0
                    } else {
                        status = b
                        expected = if ((b and 0xF0) == 0xC0 || (b and 0xF0) == 0xD0) 1 else 2
                    }
                }
                else -> {
                    if (inSysex) {
                        sysex.write(b)
                    } else if (status != 0) {
                        buf[count++] = b.toByte()
                        if (count == expected) {
                            val msg = ByteArray(expected + 1)
                            msg[0] = status.toByte()
                            for (j in 0 until expected) msg[j + 1] = buf[j]
                            count = 0
                            emit(msg, t)
                        }
                    }
                }
            }
        }
    }
}
