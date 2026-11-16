package es.jorgetejero.praeludium

import kotlin.math.roundToLong

/** Graba lo que manda el órgano (notas, registros, combinaciones, volúmenes). */
class Recorder {

    private val lock = Any()
    @Volatile var recording = false
        private set
    private var startNs = 0L
    private val events = ArrayList<Pair<Long, ByteArray>>()
    private val held = LinkedHashSet<Int>()

    // Estado del lector de bytes MIDI
    private var status = 0
    private var expected = 0
    private val buf = ArrayList<Byte>()
    private var inSysex = false
    private val sysex = ArrayList<Byte>()

    val elapsedSeconds: Double
        get() = if (recording) (System.nanoTime() - startNs) / 1e9 else 0.0

    val count: Int
        get() = synchronized(lock) { events.size }

    fun start() {
        synchronized(lock) {
            events.clear()
            held.clear()
            status = 0
            expected = 0
            buf.clear()
            inSysex = false
            sysex.clear()
            startNs = System.nanoTime()
            recording = true
        }
    }

    fun feed(data: ByteArray, offset: Int, count: Int, timestamp: Long) {
        if (!recording) return
        synchronized(lock) {
            if (!recording) return
            val t = (if (timestamp > 0) timestamp else System.nanoTime()).coerceAtLeast(startNs)
            for (i in offset until offset + count) {
                val b = data[i].toInt() and 0xFF
                when {
                    b >= 0xF8 -> { /* reloj, active sensing, start/stop: no se graban */ }
                    b == 0xF0 -> {
                        inSysex = true
                        sysex.clear()
                        sysex.add(0xF0.toByte())
                    }
                    b == 0xF7 -> {
                        if (inSysex) {
                            sysex.add(0xF7.toByte())
                            add(t, sysex.toByteArray())
                            inSysex = false
                        }
                    }
                    b >= 0x80 -> {
                        inSysex = false
                        buf.clear()
                        if (b >= 0xF0) {
                            status = 0 // mensajes comunes de sistema: no se graban
                        } else {
                            status = b
                            expected = if ((b and 0xF0) == 0xC0 || (b and 0xF0) == 0xD0) 1 else 2
                        }
                    }
                    else -> {
                        if (inSysex) {
                            sysex.add(b.toByte())
                        } else if (status != 0) {
                            buf.add(b.toByte())
                            if (buf.size == expected) {
                                val msg = ByteArray(expected + 1)
                                msg[0] = status.toByte()
                                for (j in 0 until expected) msg[j + 1] = buf[j]
                                buf.clear()
                                add(t, msg)
                            }
                        }
                    }
                }
            }
        }
    }

    private fun add(t: Long, msg: ByteArray) {
        val st = msg[0].toInt() and 0xFF
        val type = st and 0xF0
        if ((type == 0x90 || type == 0x80) && msg.size >= 3) {
            val key = (st and 0x0F) * 128 + (msg[1].toInt() and 0x7F)
            if (type == 0x90 && (msg[2].toInt() and 0x7F) > 0) held.add(key) else held.remove(key)
        }
        events.add(Pair(t, msg))
    }

    /**
     * Termina la grabación. Devuelve los mensajes con su tiempo en ticks (960 por segundo),
     * o null si no ha llegado nada.
     */
    fun stop(): List<Pair<Long, ByteArray>>? {
        synchronized(lock) {
            if (!recording) return null
            recording = false
            if (events.isEmpty()) return null
            val endNs = System.nanoTime()
            for (key in held) {
                events.add(Pair(endNs, byteArrayOf((0x80 or (key / 128)).toByte(), (key % 128).toByte(), 0)))
            }
            held.clear()
            val sorted = events.sortedBy { it.first }
            // Se quita el silencio del principio (se deja un tercio de segundo).
            val base = maxOf(startNs, sorted.first().first - 300_000_000L)
            val result = sorted.map { (t, d) -> Pair(((t - base) * 960.0 / 1e9).roundToLong().coerceAtLeast(0), d) }
            events.clear()
            return result
        }
    }
}
