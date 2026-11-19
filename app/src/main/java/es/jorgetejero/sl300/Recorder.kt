package es.jorgetejero.sl300

import kotlin.math.roundToLong

/** Graba lo que manda el órgano (notas, registros, combinaciones, volúmenes). */
class Recorder {

    private val lock = Any()
    @Volatile var recording = false
        private set
    private var startNs = 0L
    private val events = ArrayList<Pair<Long, ByteArray>>()
    private val held = LinkedHashSet<Int>()
    private val parser = MidiParser { msg, t -> add(t, msg) }

    val elapsedSeconds: Double
        get() = if (recording) (System.nanoTime() - startNs) / 1e9 else 0.0

    val count: Int
        get() = synchronized(lock) { events.size }

    fun start() {
        synchronized(lock) {
            events.clear()
            held.clear()
            parser.reset()
            startNs = System.nanoTime()
            recording = true
        }
    }

    fun feed(data: ByteArray, offset: Int, count: Int, timestamp: Long) {
        if (!recording) return
        synchronized(lock) {
            if (!recording) return
            val t = (if (timestamp > 0) timestamp else System.nanoTime()).coerceAtLeast(startNs)
            parser.feed(data, offset, count, t)
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
