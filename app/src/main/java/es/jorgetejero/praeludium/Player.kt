package es.jorgetejero.praeludium

import java.util.concurrent.locks.LockSupport
import kotlin.math.roundToInt

/**
 * Reproductor MIDI con tempo, tono, volumen por secciones, repetición de un tramo
 * y saltos en la línea de tiempo. Todo lo que suena sale por [send].
 */
class Player(private val send: (ByteArray) -> Unit) {

    companion object {
        /** Canales de las secciones: Recitativo, Gran Órgano y Pedal. */
        val SECTIONS = intArrayOf(0, 1, 3)
    }

    private val lock = Any()

    @Volatile var song: Song? = null
        private set
    @Volatile var playing = false
        private set
    @Volatile var tempo = 1.0
    @Volatile var transpose = 0
        private set
    /** Volumen de la app (0..1): escala el volumen de cada sección (CC7 de canales 1, 2 y 4). */
    @Volatile var volume = 1.0
        private set
    @Volatile var fading = false
        private set
    /** Al parar o terminar, apagar los registros que puso la pieza y volver a la registración manual. */
    @Volatile var cleanOnStop = true
    @Volatile var loopStartUs: Double? = null
        private set
    @Volatile var loopEndUs: Double? = null
        private set

    var onFinished: (() -> Unit)? = null
    var onSent: ((ByteArray) -> Unit)? = null

    @Volatile private var posUs = 0.0
    @Volatile private var fadeFactor = 1.0
    private var idx = 0
    private var lastNanos = 0L
    private var pendingSeekUs: Double? = null
    private var touchedRegistration = false

    private val active = HashMap<Int, Int>()
    private val sounding = HashMap<Int, Int>()
    private val fileVol = IntArray(16) { -1 }
    private val played = StopTracker()

    private val thread = Thread({ loop() }, "reproductor").apply {
        isDaemon = true
        priority = Thread.MAX_PRIORITY
        start()
    }

    val positionUs: Double get() = posUs
    val durationUs: Double get() = (song?.durationUs ?: 0L).toDouble()

    // --------------------------------------------------------------- órdenes

    fun load(s: Song) {
        synchronized(lock) {
            playing = false
            allOff()
            if (cleanOnStop) cleanOrgan()
            song = s
            idx = 0
            posUs = 0.0
            pendingSeekUs = null
            loopStartUs = null
            loopEndUs = null
            fileVol.fill(-1)
        }
    }

    fun play() {
        synchronized(lock) {
            val s = song ?: return
            if (idx >= s.size && posUs >= s.durationUs) {
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
            loopStartUs = null
            loopEndUs = null
            if (cleanOnStop) cleanOrgan()
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

    fun setVolume(v: Double) {
        synchronized(lock) {
            volume = v.coerceIn(0.0, 1.0)
            applyVolume()
        }
    }

    /** Pone el volumen de la app sin mandar nada al órgano (al abrir la app). */
    fun setVolumeSilently(percent: Int) {
        volume = (percent / 100.0).coerceIn(0.0, 1.0)
    }

    fun setLoop(startUs: Double, endUs: Double) {
        synchronized(lock) {
            if (endUs <= startUs) return
            loopStartUs = startUs
            loopEndUs = endUs
        }
    }

    fun clearLoop() {
        synchronized(lock) {
            loopStartUs = null
            loopEndUs = null
        }
    }

    /** Baja el volumen de las secciones en 5 segundos, detiene y lo deja como estaba. */
    fun fadeOut(onDone: () -> Unit) {
        if (fading || !playing) return
        fading = true
        Thread {
            try {
                for (i in 1..50) {
                    synchronized(lock) {
                        fadeFactor = 1.0 - i / 50.0
                        applyVolume()
                    }
                    Thread.sleep(100)
                }
                stop()
                Thread.sleep(1500)
            } catch (e: InterruptedException) {
            } finally {
                synchronized(lock) {
                    fadeFactor = 1.0
                    applyVolume()
                }
                fading = false
                onDone()
            }
        }.start()
    }

    // --------------------------------------------------------------- bucle

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
                        val le = loopEndUs
                        val ls = loopStartUs
                        while (idx < s.size && s.timeUs[idx] <= posUs && (le == null || s.timeUs[idx] < le)) {
                            dispatch(s.data[idx])
                            idx++
                        }
                        if (le != null && ls != null && posUs >= le) {
                            doSeek(s, ls)
                            sleepNs = 200_000L
                        } else if (idx >= s.size) {
                            if (posUs >= s.durationUs) {
                                playing = false
                                allOff()
                                if (cleanOnStop) cleanOrgan()
                                posUs = s.durationUs.toDouble()
                                finished = true
                            } else {
                                sleepNs = (((s.durationUs - posUs) / tempo) * 1000.0).toLong()
                                    .coerceIn(200_000L, 5_000_000L)
                            }
                        } else {
                            var next = s.timeUs[idx].toDouble()
                            if (le != null && le < next) next = le
                            sleepNs = (((next - posUs) / tempo) * 1000.0).toLong()
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

    // --------------------------------------------------------------- envío

    private fun out(d: ByteArray) {
        send(d)
        played.feed(d)
        onSent?.invoke(d)
    }

    private fun cc(ch: Int, c: Int, v: Int) = byteArrayOf((0xB0 or ch).toByte(), c.toByte(), v.toByte())

    private fun scaled(v: Int): Int = (v * volume * fadeFactor).roundToInt().coerceIn(0, 127)

    private fun applyVolume() {
        for (ch in SECTIONS) {
            val v = if (fileVol[ch] >= 0) fileVol[ch] else 127
            out(cc(ch, 7, scaled(v)))
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
        dispatchControl(d)
    }

    /** Todo lo que no son notas: registros, combinaciones, volúmenes, SysEx. */
    private fun dispatchControl(d: ByteArray) {
        val status = d[0].toInt() and 0xFF
        val type = status and 0xF0
        val ch = status and 0x0F
        if (type == 0xB0 && d.size >= 3) {
            val c = d[1].toInt() and 0x7F
            if (c == 7 && ch != 15) {
                fileVol[ch] = d[2].toInt() and 0x7F
                out(cc(ch, 7, scaled(fileVol[ch])))
                return
            }
            if (c == 99 || c == 98 || c == 6 || c == 92) touchedRegistration = true
        }
        if (type == 0xC0 && ch == 15) touchedRegistration = true
        out(d)
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
        for (ch in 0 until 16) send(cc(ch, 123, 0))
    }

    private fun pace(count: Int) {
        if (count % 16 == 0) {
            try { Thread.sleep(3) } catch (e: InterruptedException) { }
        }
    }

    /**
     * Deja el órgano como marcan sus placas: apaga los registros que encendió la pieza
     * (el órgano no enciende la luz de las placas con lo que llega por MIDI) y manda el pistón 0.
     */
    private fun cleanOrgan() {
        if (!touchedRegistration && played.isEmpty()) return
        var n = 0
        for (k in played.stopsOn()) {
            val ch = k / 16384
            val bank = (k / 128) % 128
            val i = k % 128
            out(cc(ch, 99, 0))
            out(cc(ch, 98, bank))
            out(cc(ch, 6, i))
            pace(++n)
        }
        for (ch in played.tremoloChannels()) out(cc(ch, 92, 0))
        out(cc(15, 0, 64))
        out(cc(15, 32, 0))
        out(byteArrayOf(0xCF.toByte(), 0))
        played.clear()
        touchedRegistration = false
    }

    private fun doSeek(s: Song, target: Double) {
        silenceSounding()
        for (ch in 0 until 16) send(cc(ch, 123, 0))
        active.clear()
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
                        val vel = d[2].toInt() and 0x7F
                        if (type == 0x90 && vel > 0) active[key] = vel else active.remove(key)
                    }
                    type == 0xB0 && d.size >= 3 && (d[1].toInt() and 0x7F) !in ordered -> {
                        val key = ch * 128 + (d[1].toInt() and 0x7F)
                        lastCc.remove(key)
                        lastCc[key] = d
                    }
                    else -> {
                        dispatchControl(d)
                        pace(++count)
                    }
                }
            }
            i++
        }
        for (d in lastCc.values) {
            dispatchControl(d)
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
