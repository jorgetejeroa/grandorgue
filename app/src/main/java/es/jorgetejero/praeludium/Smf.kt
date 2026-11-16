package es.jorgetejero.praeludium

import java.io.ByteArrayOutputStream

/** Una pieza lista para reproducir: mensajes MIDI ordenados con su tiempo en microsegundos (a tempo 100 %). */
class Song(val name: String, val timeUs: LongArray, val data: List<ByteArray>, val durationUs: Long) {
    val size: Int get() = data.size
}

/** Lectura y escritura de archivos MIDI estándar (.mid). */
object Smf {

    private class Reader(val b: ByteArray) {
        var pos = 0
        val remaining: Int get() = b.size - pos
        fun u8(): Int = b[pos++].toInt() and 0xFF
        fun peek(): Int = b[pos].toInt() and 0xFF
        fun u16(): Int {
            val hi = u8()
            val lo = u8()
            return (hi shl 8) or lo
        }
        fun u32(): Long {
            val hi = u16().toLong()
            val lo = u16().toLong()
            return (hi shl 16) or lo
        }
        fun str4(): String {
            val s = String(b, pos, 4, Charsets.ISO_8859_1)
            pos += 4
            return s
        }
        fun vlq(): Long {
            var v = 0L
            for (i in 0 until 4) {
                val c = u8()
                v = (v shl 7) or (c and 0x7F).toLong()
                if ((c and 0x80) == 0) break
            }
            return v
        }
        fun bytes(n: Int): ByteArray {
            val m = n.coerceIn(0, remaining)
            val r = b.copyOfRange(pos, pos + m)
            pos += m
            return r
        }
    }

    private class Raw(val tick: Long, val order: Int, val data: ByteArray)

    private fun dataLength(status: Int): Int = when (status and 0xF0) {
        0xC0, 0xD0 -> 1
        else -> 2
    }

    fun parse(bytes: ByteArray, name: String): Song {
        val r = Reader(bytes)
        if (r.remaining < 14 || r.str4() != "MThd") throw IllegalArgumentException("No es un archivo MIDI")
        val headerLen = r.u32().toInt()
        r.u16() // formato
        r.u16() // número de pistas
        val division = r.u16()
        r.pos += (headerLen - 6).coerceAtLeast(0)

        val raws = ArrayList<Raw>()
        val tempos = ArrayList<LongArray>() // [tick, microsegundos por negra]
        var maxTick = 0L
        var order = 0

        while (r.remaining >= 8) {
            val id = r.str4()
            val len = r.u32().toInt()
            val end = (r.pos + len).coerceAtMost(bytes.size)
            if (id != "MTrk") {
                r.pos = end
                continue
            }
            var tick = 0L
            var running = 0
            try {
                while (r.pos < end) {
                    tick += r.vlq()
                    var status = r.peek()
                    if ((status and 0x80) != 0) {
                        r.pos++
                    } else {
                        status = running
                        if (status == 0) break
                    }
                    when (status) {
                        0xFF -> {
                            val type = r.u8()
                            val l = r.vlq().toInt()
                            val d = r.bytes(l)
                            if (type == 0x51 && d.size == 3) {
                                val us = ((d[0].toLong() and 0xFF) shl 16) or
                                    ((d[1].toLong() and 0xFF) shl 8) or (d[2].toLong() and 0xFF)
                                if (us > 0) tempos.add(longArrayOf(tick, us))
                            }
                            if (type == 0x2F) break
                        }
                        0xF0 -> {
                            val l = r.vlq().toInt()
                            val d = r.bytes(l)
                            val msg = ByteArray(d.size + 1)
                            msg[0] = 0xF0.toByte()
                            System.arraycopy(d, 0, msg, 1, d.size)
                            raws.add(Raw(tick, order++, msg))
                        }
                        0xF7 -> {
                            val l = r.vlq().toInt()
                            val d = r.bytes(l)
                            if (d.isNotEmpty()) raws.add(Raw(tick, order++, d))
                        }
                        else -> {
                            if (status >= 0xF0) {
                                // Mensaje de sistema inesperado dentro del archivo: se ignora.
                                continue
                            }
                            running = status
                            val n = dataLength(status)
                            val msg = ByteArray(n + 1)
                            msg[0] = status.toByte()
                            for (i in 1..n) msg[i] = (r.u8() and 0x7F).toByte()
                            raws.add(Raw(tick, order++, msg))
                        }
                    }
                }
            } catch (e: IndexOutOfBoundsException) {
                // Pista cortada: nos quedamos con lo leído.
            }
            if (tick > maxTick) maxTick = tick
            r.pos = end
        }

        raws.sortWith(compareBy<Raw>({ it.tick }, { it.order }))
        tempos.sortBy { it[0] }

        val n = raws.size
        val times = LongArray(n)
        val data = ArrayList<ByteArray>(n)

        val toUs: (Long) -> Double
        if ((division and 0x8000) != 0) {
            val fps = -((division shr 8).toByte().toInt())
            val tpf = division and 0xFF
            val usPerTick = 1_000_000.0 / (fps * tpf).coerceAtLeast(1)
            toUs = { t -> t * usPerTick }
        } else {
            val ppq = division.coerceAtLeast(1).toDouble()
            // Tramos de tempo: tick de inicio, microsegundos en ese tick, microsegundos por negra.
            val segTick = ArrayList<Long>()
            val segUs = ArrayList<Double>()
            val segTempo = ArrayList<Long>()
            segTick.add(0L); segUs.add(0.0); segTempo.add(500_000L)
            for (t in tempos) {
                val last = segTick.size - 1
                val us = segUs[last] + (t[0] - segTick[last]) * segTempo[last] / ppq
                if (t[0] == segTick[last]) {
                    segTempo[last] = t[1]
                } else {
                    segTick.add(t[0]); segUs.add(us); segTempo.add(t[1])
                }
            }
            toUs = { t ->
                var i = segTick.size - 1
                while (i > 0 && segTick[i] > t) i--
                segUs[i] + (t - segTick[i]) * segTempo[i] / ppq
            }
        }

        for (i in 0 until n) {
            times[i] = toUs(raws[i].tick).toLong()
            data.add(raws[i].data)
        }
        val duration = maxOf(toUs(maxTick).toLong(), if (n > 0) times[n - 1] else 0L)
        return Song(name, times, data, duration)
    }

    /** Escribe un MIDI tipo 0 a 480 ticks por negra y 120 negras por minuto (960 ticks = 1 segundo). */
    fun write(events: List<Pair<Long, ByteArray>>, title: String): ByteArray {
        val tr = ByteArrayOutputStream()
        fun vlq(value: Long) {
            var v = value.coerceAtLeast(0)
            val parts = ArrayList<Int>()
            parts.add((v and 0x7F).toInt())
            v = v shr 7
            while (v > 0) {
                parts.add(((v and 0x7F) or 0x80).toInt())
                v = v shr 7
            }
            for (i in parts.indices.reversed()) tr.write(parts[i])
        }
        val titleBytes = title.toByteArray(Charsets.UTF_8)
        vlq(0); tr.write(0xFF); tr.write(0x03); vlq(titleBytes.size.toLong()); tr.write(titleBytes, 0, titleBytes.size)
        vlq(0); tr.write(0xFF); tr.write(0x51); tr.write(0x03); tr.write(0x07); tr.write(0xA1); tr.write(0x20)
        vlq(0); tr.write(0xFF); tr.write(0x58); tr.write(0x04); tr.write(0x04); tr.write(0x02); tr.write(0x18); tr.write(0x08)
        var last = 0L
        for ((tick, d) in events) {
            if (d.isEmpty()) continue
            vlq(tick - last)
            last = tick
            if ((d[0].toInt() and 0xFF) == 0xF0) {
                tr.write(0xF0)
                vlq((d.size - 1).toLong())
                tr.write(d, 1, d.size - 1)
            } else {
                tr.write(d, 0, d.size)
            }
        }
        vlq(0); tr.write(0xFF); tr.write(0x2F); tr.write(0x00)

        val body = tr.toByteArray()
        val out = ByteArrayOutputStream()
        fun be32(v: Int) {
            out.write((v ushr 24) and 0xFF); out.write((v ushr 16) and 0xFF)
            out.write((v ushr 8) and 0xFF); out.write(v and 0xFF)
        }
        out.write('M'.code); out.write('T'.code); out.write('h'.code); out.write('d'.code)
        be32(6)
        out.write(0); out.write(0) // formato 0
        out.write(0); out.write(1) // una pista
        out.write(0x01); out.write(0xE0) // 480 ticks por negra
        out.write('M'.code); out.write('T'.code); out.write('r'.code); out.write('k'.code)
        be32(body.size)
        out.write(body, 0, body.size)
        return out.toByteArray()
    }
}
