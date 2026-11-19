package es.jorgetejero.sl300

import java.util.Locale
import kotlin.math.roundToLong

/** Marcador con nombre: posición en milisegundos de la pieza a tempo 100 %. Mismo formato que Grand Orgue. */
data class Marker(var posMs: Double, var name: String)

object Markers {
    fun parse(text: String): MutableList<Marker> {
        val result = ArrayList<Marker>()
        for (raw in text.split('\n')) {
            val line = raw.trimEnd('\r')
            val tab = line.indexOf('\t')
            if (tab <= 0) continue
            val pos = line.substring(0, tab).trim().toDoubleOrNull() ?: continue
            if (pos < 0) continue
            result.add(Marker(pos, line.substring(tab + 1).trimEnd()))
        }
        result.sortBy { it.posMs }
        return result
    }

    fun serialize(list: List<Marker>): String {
        val sb = StringBuilder()
        for (m in list.sortedBy { it.posMs }) {
            sb.append(m.posMs.roundToLong()).append('\t').append(m.name).append('\n')
        }
        return sb.toString()
    }
}

/** Lista preparada (Misa o Libre). Mismo formato `.golist` que Grand Orgue. */
class GoList(var isMass: Boolean, val items: MutableList<Pair<String, String>>) {

    companion object {
        val MOMENTS = listOf(
            "Entrada", "Kyrie", "Gloria", "Salmo", "Aleluya", "Ofertorio", "Santo",
            "Padrenuestro", "Cordero de Dios", "Comunión", "Acción de gracias", "Final"
        )
        private val OPTIONAL = booleanArrayOf(
            false, true, true, true, false, false, false, true, true, false, true, false
        )

        fun parse(text: String): GoList {
            var mass = false
            val items = ArrayList<Pair<String, String>>()
            for (raw in text.removePrefix("﻿").split('\n')) {
                val line = raw.trimEnd('\r')
                val tab = line.indexOf('\t')
                if (line.startsWith("type=")) {
                    mass = line.substring(5).trim() == "mass"
                } else if (tab >= 0 && tab + 1 < line.length) {
                    items.add(Pair(line.substring(0, tab), line.substring(tab + 1).trimEnd()))
                }
            }
            return GoList(mass, items)
        }

        /** Propone el siguiente momento obligatorio de la Misa (igual que Grand Orgue). */
        fun proposeNext(previous: String?): String? {
            val prev = if (previous.isNullOrEmpty()) -1 else MOMENTS.indexOf(previous)
            if (!previous.isNullOrEmpty() && prev < 0) return null
            var next = prev + 1
            while (next < MOMENTS.size && OPTIONAL[next]) next++
            return if (next < MOMENTS.size) MOMENTS[next] else null
        }

        fun cleanName(name: String): String = name.replace(Regex("[\\\\/:*?\"<>|]"), "-").trim()
    }

    fun serialize(): String {
        val sb = StringBuilder()
        sb.append(if (isMass) "type=mass" else "type=free").append('\n')
        for ((moment, file) in items) sb.append(moment).append('\t').append(file).append('\n')
        return sb.toString()
    }
}

/** Letra de un canto dividida en bloques con nombre: [Estribillo], [Estrofa 1]… */
class Lyrics(val blocks: List<Pair<String, String>>) {

    companion object {
        private val HEADER = Regex("^\\s*\\[(.+)]\\s*$")

        fun parse(text: String): Lyrics {
            val blocks = ArrayList<Pair<String, String>>()
            var name: String? = null
            val sb = StringBuilder()
            fun flush() {
                val body = sb.toString().trim('\n', '\r', ' ')
                val n = name
                if (n != null) blocks.add(Pair(n, body))
                else if (body.isNotEmpty()) blocks.add(Pair("Letra", body))
                sb.setLength(0)
            }
            for (raw in text.removePrefix("﻿").split('\n')) {
                val line = raw.trimEnd('\r')
                val m = HEADER.find(line)
                if (m != null) {
                    flush()
                    name = m.groupValues[1].trim()
                } else {
                    sb.append(line).append('\n')
                }
            }
            flush()
            return Lyrics(blocks)
        }

        const val TEMPLATE = "[Estrofa 1]\n\n\n[Estribillo]\n\n\n[Estrofa 2]\n\n"

        private fun norm(s: String) = s.trim().lowercase(Locale("es", "ES"))
        fun isBlack(name: String) = norm(name) == "negro"
        fun isTitle(name: String) = norm(name) == "título" || norm(name) == "titulo"
    }

    fun indexOf(name: String): Int {
        val n = norm(name)
        return blocks.indexOfFirst { norm(it.first) == n }
    }
}
