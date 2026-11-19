package es.jorgetejero.praeludium

/** Un registro del Praeludium III de San Miguel. Canal 0 = Recitativo, 1 = Gran Órgano, 3 = Pedal. */
data class StopDef(val num: Int, val name: String, val ch: Int, val bank: Int, val idx: Int) {
    val isTremolo: Boolean get() = bank < 0
    val key: Int get() = Stops.key(ch, bank, idx)
}

object Stops {
    fun key(ch: Int, bank: Int, idx: Int): Int = ch * 16384 + bank * 128 + idx

    val pedal = listOf(
        StopDef(1, "Principale 16'", 3, 4, 4),
        StopDef(2, "Subbasso 16'", 3, 4, 66),
        StopDef(3, "Ottava 8'", 3, 4, 8),
        StopDef(4, "Bordone 8'", 3, 4, 69),
        StopDef(5, "Decima Quinta 4'", 3, 4, 12),
        StopDef(6, "Trombone 16'", 3, 5, 4),
        StopDef(7, "Clarone 4'", 3, 5, 13),
        StopDef(8, "Unione I-Ped.", 3, 6, 4),
        StopDef(9, "Unione II-Ped.", 3, 6, 5),
        StopDef(31, "Reg. access. Ped.", 3, 6, 0)
    )

    val great = listOf(
        StopDef(10, "Bordone 16'", 1, 1, 64),
        StopDef(11, "Principale 8'", 1, 0, 3),
        StopDef(12, "Flauto a camino 8'", 1, 1, 5),
        StopDef(13, "Ottava 4'", 1, 0, 9),
        StopDef(14, "Flauto a cuspide 4'", 1, 1, 4),
        StopDef(15, "Flauto in XII", 1, 1, 17),
        StopDef(16, "Decima Quinta 2'", 1, 0, 14),
        StopDef(17, "Ripieno 4 F.", 1, 0, 65),
        StopDef(18, "Tromba 8'", 1, 3, 5),
        StopDef(19, "Unione II-I", 1, 6, 9),
        StopDef(29, "Reg. access. G.O.", 1, 6, 0)
    )

    val swell = listOf(
        StopDef(20, "Bordone 8'", 0, 1, 68),
        StopDef(21, "Viola da gamba 8'", 0, 2, 3),
        StopDef(22, "Principale 4'", 0, 0, 9),
        StopDef(23, "Flauto a camino 4'", 0, 1, 12),
        StopDef(24, "Cimbalo 3 F.", 0, 0, 68),
        StopDef(25, "Cornetto 3 F.", 0, 0, 100),
        StopDef(26, "Voce celeste 8'", 0, 2, 65),
        StopDef(27, "Oboe 8'", 0, 3, 7),
        StopDef(28, "Tremolo", 0, -1, -1),
        StopDef(30, "Reg. access. Rec.", 0, 6, 0)
    )

    val all: List<StopDef> = pedal + great + swell
    private val byKey: Map<Int, StopDef> = all.filter { !it.isTremolo }.associateBy { it.key }

    fun find(key: Int): StopDef? = byKey[key]

    fun sectionName(ch: Int): String = when (ch) {
        0 -> "Recitativo"
        1 -> "Gran Órgano"
        3 -> "Pedal"
        else -> "Canal ${ch + 1}"
    }
}

/** Sigue qué registros están puestos según los mensajes MIDI (NRPN y trémolo). */
class StopTracker {
    private val msb = IntArray(16) { -1 }
    private val lsb = IntArray(16) { -1 }
    private val on = LinkedHashSet<Int>()
    private val tremolo = BooleanArray(16)

    @Synchronized
    fun feed(d: ByteArray) {
        if (d.size < 3) return
        val st = d[0].toInt() and 0xFF
        if ((st and 0xF0) != 0xB0) return
        val ch = st and 0x0F
        val c = d[1].toInt() and 0x7F
        val v = d[2].toInt() and 0x7F
        when (c) {
            99 -> msb[ch] = v
            98 -> lsb[ch] = v
            6 -> if (lsb[ch] >= 0) {
                val k = Stops.key(ch, lsb[ch], v)
                if (msb[ch] == 64) on.add(k) else on.remove(k)
            }
            92 -> tremolo[ch] = v > 0
        }
    }

    @Synchronized
    fun stopsOn(): List<Int> = on.toList()

    @Synchronized
    fun tremoloOn(ch: Int): Boolean = tremolo[ch]

    @Synchronized
    fun tremoloChannels(): List<Int> = (0 until 16).filter { tremolo[it] }

    @Synchronized
    fun isEmpty(): Boolean = on.isEmpty() && tremolo.none { it }

    @Synchronized
    fun clear() {
        on.clear()
        tremolo.fill(false)
        msb.fill(-1)
        lsb.fill(-1)
    }
}
