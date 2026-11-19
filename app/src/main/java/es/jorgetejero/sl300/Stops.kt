package es.jorgetejero.sl300

/**
 * Un registro del Ahlborn SL 300 de San Adrián.
 * Canal (contando desde 0): 0 = Recitativo III, 1 = Grand'Organo II, 2 = Positivo I, 3 = Pedal.
 * Por MIDI: CC70 en el canal de la sección, valor = índice + 64 enciende, valor = índice apaga.
 * Los trémolos (idx < 0) van por CC92 (127 / 0).
 */
data class StopDef(val num: Int, val name: String, val ch: Int, val idx: Int) {
    val isTremolo: Boolean get() = idx < 0
    val key: Int get() = Stops.key(ch, idx)
}

object Stops {
    const val REC = 0
    const val GO = 1
    const val POS = 2
    const val PED = 3

    /** CC de los registros y de los trémolos. */
    const val CC_STOP = 70
    const val CC_TREMOLO = 92

    /** Canales que tienen trémolo: Positivo y Recitativo. */
    val TREMOLO_CHANNELS = intArrayOf(POS, REC)

    fun key(ch: Int, idx: Int): Int = ch * 128 + idx

    val pedal = listOf(
        StopDef(1, "Principale 16'", PED, 1),
        StopDef(2, "Subbasso 16'", PED, 2),
        StopDef(3, "Ottava 8'", PED, 3),
        StopDef(4, "Bordone 8'", PED, 4),
        StopDef(5, "Decima quinta 4'", PED, 5),
        StopDef(6, "Flauto aperto 4'", PED, 6),
        StopDef(7, "Ripieno 4 F.", PED, 8),
        StopDef(8, "Trombone 16'", PED, 10),
        StopDef(9, "Tromba 8'", PED, 11),
        StopDef(10, "Regale 4'", PED, 12),
        StopDef(11, "Unione I-Ped.", PED, 21),
        StopDef(12, "Unione II-Ped.", PED, 13),
        StopDef(13, "Unione III-Ped.", PED, 14)
    )

    val great = listOf(
        StopDef(14, "Quintadena 16'", GO, 2),
        StopDef(15, "Principale 8'", GO, 3),
        StopDef(16, "Corno di camoscio 8'", GO, 0),
        StopDef(17, "Ottava 4'", GO, 4),
        StopDef(18, "Flauto a camino 4'", GO, 1),
        StopDef(19, "Decima quinta 2'", GO, 5),
        StopDef(20, "Ripieno 4 F.", GO, 9),
        StopDef(21, "Voce umana 8'", GO, 11),
        StopDef(22, "Fagotto 16'", GO, 12),
        StopDef(23, "Tromba 8'", GO, 13),
        StopDef(24, "Unione I-II", GO, 24),
        StopDef(25, "Unione III-II", GO, 16)
    )

    val positive = listOf(
        StopDef(26, "Flauto a camino 8'", POS, 0),
        StopDef(27, "Principale 4'", POS, 4),
        StopDef(28, "Flauto a cuspide 4'", POS, 3),
        StopDef(29, "Ottava 2'", POS, 7),
        StopDef(30, "Quinta 1 1/3'", POS, 8),
        StopDef(31, "Ripieno 3 F.", POS, 10),
        StopDef(32, "Sesquialtera 2 F.", POS, 11),
        StopDef(33, "Regale 16'", POS, 14),
        StopDef(34, "Cromorno 8'", POS, 16),
        StopDef(35, "Tremolo", POS, -1),
        StopDef(36, "Unione III-I", POS, 19)
    )

    val swell = listOf(
        StopDef(37, "Bordoncino 16'", REC, 0),
        StopDef(38, "Bordone 8'", REC, 1),
        StopDef(39, "Viola da gamba 8'", REC, 10),
        StopDef(40, "Flauto conico 4'", REC, 2),
        StopDef(41, "Nazardo 2 2/3'", REC, 8),
        StopDef(42, "Flautino 2'", REC, 4),
        StopDef(43, "Terza 1 3/5'", REC, 9),
        StopDef(44, "Cimbalo 3 F.", REC, 12),
        StopDef(45, "Voce celeste 8'", REC, 11),
        StopDef(46, "Oboe 8'", REC, 17),
        StopDef(47, "Tremolo", REC, -1)
    )

    /** Secciones en el orden en que se muestran en pantalla. */
    val sections: List<Pair<String, List<StopDef>>> = listOf(
        Pair("Pedal", pedal),
        Pair("Positivo I", positive),
        Pair("Grand'Organo II", great),
        Pair("Recitativo III", swell)
    )

    val all: List<StopDef> = pedal + great + positive + swell
    private val byKey: Map<Int, StopDef> = all.filter { !it.isTremolo }.associateBy { it.key }

    fun find(key: Int): StopDef? = byKey[key]

    fun sectionName(ch: Int): String = when (ch) {
        REC -> "Recitativo"
        GO -> "Grand'Organo"
        POS -> "Positivo"
        PED -> "Pedal"
        else -> "Canal ${ch + 1}"
    }

    /** Texto para un registro que no está en la tabla. */
    fun unknownLabel(key: Int): String = "${sectionName(key / 128)} n.º ${key % 128}"
}

/** Sigue qué registros están puestos según los mensajes MIDI (CC70 y trémolos CC92). */
class StopTracker {
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
            Stops.CC_STOP -> if (v >= 64) on.add(Stops.key(ch, v - 64)) else on.remove(Stops.key(ch, v))
            Stops.CC_TREMOLO -> tremolo[ch] = v > 0
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
    }
}
