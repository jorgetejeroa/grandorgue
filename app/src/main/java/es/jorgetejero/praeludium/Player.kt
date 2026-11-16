package es.jorgetejero.praeludium

import java.util.concurrent.locks.LockSupport

/**
 * Reproductor MIDI con tempo, tono y saltos en la línea de tiempo.
 * Todo lo que suena sale por [send] (la conexión con el órgano).
 */
class Player(private val send: (ByteArray) -> Unit) {

    private val lock = Any()

    @Volatile var song: Song? = null
        private set
    @Volatile var playing = false
        private set
    @Volatile var tempo = 1.0
    @Volatile var transpose = 0
        private set
    /** Último volumen general (CC7 del canal 16) enviado al órgano. */
    @Volatile var mainVolume = 127
    /** Mientras se termina suavemente, se ignoran los cambios de volumen general que traiga el archivo. */
    @Volatile var fading = false

    var onFinished: (() -> Unit)? = null

    @Volatile private var posUs = 0.0
    private var idx = 0
    private var lastNanos = 0L
    private var pendingSeekUs: Double? = null

    /** Notas que el archivo tiene pulsadas en este momento: canal*128+nota → velocidad. */
    private val active = HashMap<Int, Int>()
    /** Notas que de verdad están sonando en el órgano: canal*128+nota del archivo → nota enviada. */
    private val sounding = HashMap<Int, Int>()

    private val thread = Thread({ loop() }, "reproductor").apply {
        isDaemon = true
        priority = Thread.MAX_PRIORITY
        start()
    }

    val positionUs: Double get() = posUs
    val durationUs: Double get() = (song?.durationUs ?: 0L).toDouble()

    fun load(s: Song) {
        synchronized(lock) {
            playing = false
            allOff()
            song = s
            idx = 0
            posUs = 0.0
            pendingSeekUs = null
        }
    }

    fun play() {
        synchronized(lock) {
            val s = song ?: return
            if (idx >= s.size) {
                idx = 0
                posUs = 0.0
                active.clear()
            }
            if (!playing) {
                resound()
                lastNanos = System.nanoTime()
                playing = true
            }
        }
        LockSupport.unpark(thread)
    }

    fun pause() {
        synchronized(lock) {
            if (playing) {
                playing = false
                silenceSounding()
            }
        }
    }

    fun stop() {
        synchronized(lock) {
            playing = false
            allOff()
            idx = 0
            posUs = 0.0
            pendingSeekUs = null
        }
    }

    fun seekTo(us: Double) {
        synchronized(lock) {
            if (song == null) return
            pendingSeekUs = us.coerceAtLeast(0.0)
        }
        LockSupport.unpark(thread)
    }

    fun setTranspose(t: Int) {
        synchronized(lock) {
            if (t == transpose) return
            silenceSounding()
            transpose = t
            if (playing) resound()
        }
    }

    // ---------------------------------------------------------------

    private fun loop() {
        while (true) {
            var sleepNs = 10_000_000L
            var finished = false
            try {
                synchronized(lock) {
                    val s = song
                    val seek = pendingSeekUs
                    if (s != null && seek != null) {
                        pendingSeekUs = null
                        doSeek(s, seek)
                    }
                    if (s != null && playing) {
                        val now = System.nanoTime()
                        posUs += (now - lastNanos) / 1000.0 * tempo
                        lastNanos = now
                        while (idx < s.size && s.timeUs[idx] <= posUs) {
                            dispatch(s.data[idx])
                            idx++
                        }
                        if (idx >= s.size) {
                            if (posUs >= s.durationUs) {
                                playing = false
                                allOff()
                                posUs = s.durationUs.toDouble()
                                finished = true
                            } else {
                                sleepNs = (((s.durationUs - posUs) / tempo) * 1000.0).toLong()
                                    .coerceIn(200_000L, 5_000_000L)
                            }
                        } else {
                            sleepNs = (((s.timeUs[idx] - posUs) / tempo) * 1000.0).toLong()
                                .coerceIn(200_000L, 5_000_000L)
                        }
                    }
                }
            } catch (e: Exception) {
                // Un fallo puntual de envío no debe parar el reproductor.
            }
            if (finished) onFinished?.invoke()
            LockSupport.parkNanos(sleepNs)
        }
    }

    private fun isNote(type: Int, d: ByteArray) = (type == 0x90 || type == 0x80) && d.size >= 3

    private fun dispatch(d: ByteArray) {
        if (d.isEmpty()) return
        val status = d[0].toInt() and 0xFF
        val type = status and 0xF0
        val ch = status and 0x0F
        if (isNote(type, d)) {
            val note = d[1].toInt() and 0x7F
            val vel = d[2].toInt() and 0x7F
            val key = ch * 128 + note
            if (type == 0x90 && vel > 0) {
                active[key] = vel
                noteOn(ch, note, vel)
            } else {
                active.remove(key)
                noteOff(ch, note)
            }
            return
        }
        if (type == 0xB0 && d.size >= 3 && ch == 15 && (d[1].toInt() and 0x7F) == 7) {
            if (fading) return
            mainVolume = d[2].toInt() and 0x7F
        }
        send(d)
    }

    private fun noteOn(ch: Int, note: Int, vel: Int) {
        val shift = if (ch == 15) 0 else transpose
        val k = note + shift
        if (k < 0 || k > 127) return
        val key = ch * 128 + note
        sounding[key]?.let { send(byteArrayOf((0x80 or ch).toByte(), it.toByte(), 0)) }
        sounding[key] = k
        send(byteArrayOf((0x90 or ch).toByte(), k.toByte(), vel.toByte()))
    }

    private fun noteOff(ch: Int, note: Int) {
        val k = sounding.remove(ch * 128 + note) ?: return
        send(byteArrayOf((0x80 or ch).toByte(), k.toByte(), 0))
    }

    /** Vuelve a pulsar las notas que el archivo tiene pulsadas (al continuar, al saltar o al cambiar de tono). */
    private fun resound() {
        for ((key, vel) in active) noteOn(key / 128, key % 128, vel)
    }

    private fun silenceSounding() {
        for ((key, k) in sounding) {
            val ch = key / 128
            send(byteArrayOf((0x80 or ch).toByte(), k.toByte(), 0))
        }
        sounding.clear()
    }

    private fun allOff() {
        silenceSounding()
        active.clear()
        for (ch in 0 until 16) send(byteArrayOf((0xB0 or ch).toByte(), 123, 0))
    }

    private fun pace(count: Int) {
        if (count % 16 == 0) {
            try { Thread.sleep(3) } catch (e: InterruptedException) { }
        }
    }

    /**
     * Salta a un punto: deja el órgano como estaba en ese momento (registros, combinaciones,
     * volúmenes) y vuelve a pulsar las notas que estaban sonando.
     */
    private fun doSeek(s: Song, target: Double) {
        silenceSounding()
        for (ch in 0 until 16) send(byteArrayOf((0xB0 or ch).toByte(), 123, 0))
        active.clear()
        // Controladores normales: solo hace falta el último valor de cada uno.
        val lastCc = LinkedHashMap<Int, ByteArray>()
        val ordered = setOf(0, 6, 32, 38, 96, 97, 98, 99, 100, 101)
        var count = 0
        var i = 0
        while (i < s.size && s.timeUs[i] < target) {
            val d = s.data[i]
            if (d.isNotEmpty()) {
                val status = d[0].toInt() and 0xFF
                val type = status and 0xF0
                val ch = status and 0x0F
                when {
                    isNote(type, d) -> {
                        val key = ch * 128 + (d[1].toInt() and 0x7F)
                        if (type == 0x90 && (d[2].toInt() and 0x7F) > 0) active[key] = d[2].toInt() and 0x7F
                        else active.remove(key)
                    }
                    type == 0xB0 && d.size >= 3 && (d[1].toInt() and 0x7F) !in ordered -> {
                        val key = ch * 128 + (d[1].toInt() and 0x7F)
                        lastCc.remove(key)
                        lastCc[key] = d
                    }
                    else -> {
                        // Registros (NRPN), combinaciones (Bank + Program Change) y SysEx: en orden.
                        send(d)
                        pace(++count)
                    }
                }
            }
            i++
        }
        for (d in lastCc.values) {
            val ch = (d[0].toInt() and 0x0F)
            if (ch == 15 && (d[1].toInt() and 0x7F) == 7) {
                if (fading) continue
                mainVolume = d[2].toInt() and 0x7F
            }
            send(d)
            pace(++count)
        }
        idx = i
        posUs = target.coerceAtMost(s.durationUs.toDouble())
        if (playing) {
            resound()
            lastNanos = System.nanoTime()
        }
    }
}
