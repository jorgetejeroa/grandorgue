package es.jorgetejero.sl300

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.hardware.display.DisplayManager
import android.media.midi.MidiDeviceInfo
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.provider.DocumentsContract
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.ArrayAdapter
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlin.math.roundToInt
import kotlin.math.roundToLong

class MainActivity : Activity() {

    companion object {
        private const val REQ_FOLDER = 1
        private const val REQ_BT = 2
        private val MIDI_SERVICE_UUID: UUID = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
        private const val ACCENT = 0xFF3F51B5.toInt()
    }

    private lateinit var prefs: SharedPreferences
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var link: MidiLink
    private lateinit var player: Player
    private val recorder = Recorder()
    private val shownStops = StopTracker()
    private val inputParser = MidiParser { msg, _ -> shownStops.feed(msg) }

    // Carpeta y listas
    private var store: FolderStore? = null
    private val folderStack = ArrayList<String>()
    private val folderNames = ArrayList<String>()
    private var folderDocs: List<Doc> = emptyList()
    private var listDocs: List<Doc> = emptyList()
    private var listsDirId: String? = null
    private var currentListName: String? = null
    private var rows: List<Entry> = emptyList()
    private val durCache = ConcurrentHashMap<String, Long>()
    @Volatile private var durGen = 0

    // Pieza que suena
    private var playPieces: List<Entry> = emptyList()
    private var playDirId: String? = null
    private var currentIndex = -1
    private var nextIndex = -1
    private var markers: MutableList<Marker> = ArrayList()
    private var lyrics: Lyrics? = null
    private var pendingStart: Runnable? = null
    private var pendingStartAt = 0L

    // Letras
    private var presentation: TvPresentation? = null
    private var fullDialog: Dialog? = null
    private var fullView: LyricsView? = null
    private var manualText: String? = null
    private var lastAutoIndex = -999
    private var shownBlock = -1
    private var markingMode = false

    // Conexión
    private var disconnectedDialog: AlertDialog? = null
    private var wasDisconnected = false
    private var reconnectAttempts = 0

    private var userSeeking = false
    private var pendingBtAction: (() -> Unit)? = null
    private var pendingEvents: List<Pair<Long, ByteArray>>? = null
    private var pendingName: String? = null

    // Vistas
    private lateinit var statusText: TextView
    private lateinit var folderText: TextView
    private lateinit var listBtn: Button
    private lateinit var durationText: TextView
    private lateinit var listView: ListView
    private lateinit var listAdapter: RowsAdapter
    private lateinit var titleText: TextView
    private lateinit var nextText: TextView
    private lateinit var timeText: TextView
    private lateinit var seekBar: SeekBar
    private lateinit var markerRow: LinearLayout
    private lateinit var playBtn: Button
    private lateinit var loopBtn: Button
    private lateinit var tempoBtn: Button
    private lateinit var toneBtn: Button
    private lateinit var volumeText: TextView
    private lateinit var volumeBar: SeekBar
    private lateinit var autoCb: CheckBox
    private lateinit var gapText: TextView
    private lateinit var repeatCb: CheckBox
    private lateinit var cleanCb: CheckBox
    private val stopViews = HashMap<Int, TextView>()
    private val tremoloViews = HashMap<Int, TextView>()
    private lateinit var otherStopsText: TextView
    private lateinit var lyricsCb: CheckBox
    private lateinit var tvStatusText: TextView
    private lateinit var lyricPreview: TextView
    private lateinit var adelantoText: TextView
    private lateinit var markingPanel: LinearLayout
    private lateinit var markBtn: Button
    private lateinit var recordBtn: Button
    private lateinit var recordText: TextView

    // ================================================================== ciclo de vida

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = getSharedPreferences("ajustes", MODE_PRIVATE)
        player = Player { link.send(it) }
        player.onFinished = { ui.post { onPieceFinished() } }
        player.onSent = { shownStops.feed(it) }
        player.cleanOnStop = prefs.getBoolean("clean", true)
        player.setVolumeSilently(prefs.getInt("volume", 100))
        buildUi()
        link = MidiLink(
            this,
            { d, o, c, t ->
                recorder.feed(d, o, c, t)
                synchronized(inputParser) { inputParser.feed(d, o, c, t) }
            },
            { s -> ui.post { statusText.text = s } },
            { info -> ui.post { autoConnectUsb(info) } },
            { ui.post { onConnected() } },
            { ui.post { onDisconnected() } }
        )
        val dm = getSystemService(DISPLAY_SERVICE) as DisplayManager
        dm.registerDisplayListener(object : DisplayManager.DisplayListener {
            override fun onDisplayAdded(displayId: Int) = updatePresentation()
            override fun onDisplayRemoved(displayId: Int) = updatePresentation()
            override fun onDisplayChanged(displayId: Int) {}
        }, ui)
        restoreFolder()
        autoConnect()
        updatePresentation()
        ui.post(ticker)
    }

    override fun onDestroy() {
        ui.removeCallbacks(ticker)
        try { presentation?.dismiss() } catch (e: Exception) { }
        player.stop()
        link.close(true)
        super.onDestroy()
    }

    @Deprecated("Se mantiene para Android 10–12")
    override fun onBackPressed() {
        moveTaskToBack(true)
    }

    private val ticker = object : Runnable {
        override fun run() {
            try { refreshControls() } catch (e: Exception) { }
            ui.postDelayed(this, 200)
        }
    }

    // ================================================================== ayudas de interfaz

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun button(text: String, size: Float = 18f, onClick: () -> Unit): Button = Button(this).apply {
        this.text = text
        textSize = size
        isAllCaps = false
        minHeight = dp(56)
        setOnClickListener { onClick() }
    }

    private fun row(vararg views: View): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        for (v in views) addView(v, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
    }

    private fun label(text: String, size: Float = 16f): TextView = TextView(this).apply {
        this.text = text
        textSize = size
        setPadding(dp(4), dp(6), dp(4), dp(6))
    }

    private fun header(text: String): TextView = label(text, 19f).apply {
        setTypeface(typeface, Typeface.BOLD)
        setTextColor(ACCENT)
        setPadding(dp(4), dp(18), dp(4), dp(4))
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    private fun cc(ch: Int, c: Int, v: Int) = byteArrayOf((0xB0 or ch).toByte(), c.toByte(), v.toByte())

    private fun fmt(us: Double): String {
        val secs = (us / 1e6).toInt().coerceAtLeast(0)
        return if (secs >= 3600) String.format(Locale.ROOT, "%d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60)
        else String.format(Locale.ROOT, "%d:%02d", secs / 60, secs % 60)
    }

    private fun stripExt(name: String): String {
        val i = name.lastIndexOf('.')
        return if (i > 0) name.substring(0, i) else name
    }

    private fun askText(title: String, initial: String, onOk: (String) -> Unit) {
        val et = EditText(this).apply {
            setText(initial)
            selectAll()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        }
        AlertDialog.Builder(this)
            .setTitle(title)
            .setView(et)
            .setPositiveButton("Aceptar") { _, _ -> onOk(et.text.toString().trim()) }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ================================================================== construcción de la interfaz

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), dp(4), dp(8), dp(4))
        }
        statusText = label("Sin conectar", 17f).apply { setTypeface(typeface, Typeface.BOLD) }
        val top = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(statusText, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 2f))
            addView(button("Conectar") { showConnectDialog() }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(button("Carpeta") { pickFolder() }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        root.addView(top)

        // ---- Panel izquierdo: carpeta, listas y piezas
        folderText = label("Elige una carpeta con el botón «Carpeta»", 15f)
        listBtn = button("Lista: todas las piezas ▾", 16f) { chooseList() }
        val listTools = row(
            button("Nueva lista", 15f) { editList(null) },
            button("Editar", 15f) {
                val n = currentListName
                if (n == null) toast("Elige primero una lista") else editList(n)
            },
            button("Borrar", 15f) { deleteList() }
        )
        durationText = label("", 14f).apply { setTextColor(Color.DKGRAY) }
        listView = ListView(this)
        listAdapter = RowsAdapter()
        listView.adapter = listAdapter
        listView.setOnItemClickListener { _, _, pos, _ -> onRowClick(rows[pos], false) }
        listView.setOnItemLongClickListener { _, _, pos, _ -> onRowClick(rows[pos], true); true }
        val listPanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(folderText)
            addView(listBtn)
            addView(listTools)
            addView(durationText)
            addView(listView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        }

        // ---- Panel derecho: controles
        val c = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), 0, dp(4), dp(24))
        }
        titleText = label("Ninguna pieza cargada", 22f).apply { setTypeface(typeface, Typeface.BOLD) }
        nextText = label("", 15f)
        timeText = label("0:00 / 0:00", 18f).apply { gravity = Gravity.CENTER }
        seekBar = SeekBar(this).apply {
            max = 1000
            setPadding(dp(16), dp(12), dp(16), dp(12))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                    if (fromUser) {
                        val target = player.durationUs * p / 1000.0
                        timeText.text = "${fmt(target / player.tempo)} / ${fmt(player.durationUs / player.tempo)}"
                    }
                }
                override fun onStartTrackingTouch(sb: SeekBar) { userSeeking = true }
                override fun onStopTrackingTouch(sb: SeekBar) {
                    userSeeking = false
                    player.seekTo(player.durationUs * sb.progress / 1000.0)
                }
            })
        }
        markerRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val markerScroll = HorizontalScrollView(this).apply { addView(markerRow) }
        playBtn = button("▶ Reproducir") { playPause() }
        val transport = row(
            button("⏮ Anterior") { cancelPendingStart(); previous() },
            playBtn,
            button("⏹ Detener") { cancelPendingStart(); player.stop() },
            button("Siguiente ⏭") { cancelPendingStart(); next() }
        )
        loopBtn = button("Repetir tramo…", 16f) { loopClicked() }
        val markerTools = row(
            button("Añadir marcador", 16f) { addMarker() },
            button("Marcadores…", 16f) { editMarkers() },
            loopBtn
        )

        tempoBtn = button("Tempo 100 %") { setTempoPercent(100) }
        val tempoRow = row(
            button("Más lento") { setTempoPercent((player.tempo * 100).roundToInt() - 5) },
            tempoBtn,
            button("Más rápido") { setTempoPercent((player.tempo * 100).roundToInt() + 5) }
        )
        toneBtn = button("Tono original") { setTone(0) }
        val toneRow = row(
            button("Bajar ½ tono") { setTone(player.transpose - 1) },
            toneBtn,
            button("Subir ½ tono") { setTone(player.transpose + 1) }
        )
        val saveTempoBtn = button("Guardar con este tempo y tono", 16f) { saveWithTempoTone() }

        volumeText = label("", 16f)
        volumeBar = SeekBar(this).apply {
            max = 100
            progress = prefs.getInt("volume", 100)
            setPadding(dp(16), dp(12), dp(16), dp(12))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                    volumeText.text = "Volumen (órgano y bafles): $p %"
                    if (fromUser) {
                        player.setVolume(p / 100.0)
                        prefs.edit().putInt("volume", p).apply()
                    }
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) {}
            })
        }
        volumeText.text = "Volumen (órgano y bafles): ${volumeBar.progress} %"
        autoCb = CheckBox(this).apply {
            text = "Pasar automáticamente a la siguiente pieza (templo abierto)"
            textSize = 16f
            isChecked = prefs.getBoolean("auto", false)
            setOnCheckedChangeListener { _, ch -> prefs.edit().putBoolean("auto", ch).apply() }
        }
        gapText = label("", 16f).apply { gravity = Gravity.CENTER }
        val gapRow = row(
            button("−", 18f) { changeGap(-1) },
            gapText,
            button("+", 18f) { changeGap(1) }
        )
        updateGapText()
        repeatCb = CheckBox(this).apply {
            text = "Repetir la lista al terminar la última pieza"
            textSize = 16f
            isChecked = prefs.getBoolean("repeat", false)
            setOnCheckedChangeListener { _, ch -> prefs.edit().putBoolean("repeat", ch).apply() }
        }
        val endRow = row(
            button("Terminar suavemente") { fadeOut() },
            button("Silencio") { silence() }
        )

        // Combinaciones
        val pistons = row(*(1..5).map { n -> button("$n", 17f) { combination(n) } }.toTypedArray())
        val transposerHint = label(
            "El traspositor del órgano tiene que estar a 0 para reproducir y para grabar.", 14f
        ).apply { setTextColor(Color.GRAY) }

        // Registros
        cleanCb = CheckBox(this).apply {
            text = "Al terminar o detener, apagar los registros que puso la pieza"
            textSize = 15f
            isChecked = player.cleanOnStop
            setOnCheckedChangeListener { _, ch ->
                player.cleanOnStop = ch
                prefs.edit().putBoolean("clean", ch).apply()
            }
        }
        val stopsGrid = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for ((title, list) in Stops.sections) {
            val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            col.addView(label(title, 15f).apply { setTypeface(typeface, Typeface.BOLD) })
            for (s in list) {
                val tv = TextView(this).apply {
                    text = "${s.num}  ${s.name}"
                    textSize = 13f
                    setPadding(dp(4), dp(3), dp(4), dp(3))
                }
                if (s.isTremolo) tremoloViews[s.ch] = tv else stopViews[s.key] = tv
                col.addView(tv)
            }
            stopsGrid.addView(col, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        otherStopsText = label("", 13f)

        // Letras
        lyricsCb = CheckBox(this).apply {
            text = "Proyectar letras en la tele"
            textSize = 17f
            isChecked = prefs.getBoolean("lyrics", false)
            setOnCheckedChangeListener { _, ch ->
                prefs.edit().putBoolean("lyrics", ch).apply()
                if (!ch) closeFullScreen()
                updatePresentation()
            }
        }
        tvStatusText = label("", 14f).apply { setTextColor(Color.DKGRAY) }
        lyricPreview = label("", 15f).apply { setTypeface(typeface, Typeface.ITALIC) }
        markBtn = button("Marcar letras", 16f) { toggleMarking() }
        val lyricsTools = row(
            button("Letra de esta pieza…", 16f) { editLyrics() },
            markBtn,
            button("Pantalla completa", 16f) { openFullScreen() }
        )
        val lyricsManual = row(
            button("◀ Letra", 16f) { manualStep(-1) },
            button("Letra ▶", 16f) { manualStep(1) },
            button("Negro", 16f) { manualBlack() },
            button("Título", 16f) { manualTitle() }
        )
        adelantoText = label("", 15f).apply { gravity = Gravity.CENTER }
        val adelantoRow = row(
            button("−", 18f) { changeAdelanto(-1) },
            adelantoText,
            button("+", 18f) { changeAdelanto(1) }
        )
        updateAdelantoText()
        markingPanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
            setBackgroundColor(Color.argb(40, 255, 152, 0))
            setPadding(dp(8), dp(8), dp(8), dp(8))
        }

        // Grabación
        recordBtn = button("● Grabar") { toggleRecord() }.apply { setTextColor(Color.rgb(180, 0, 0)) }
        recordText = label("", 16f)
        val hint = label(
            "Al grabar: pon el traspositor a 0, pulsa Grabar y después pon la registración o la combinación, para que quede guardada.", 14f
        ).apply { setTextColor(Color.GRAY) }

        c.addView(titleText)
        c.addView(nextText)
        c.addView(seekBar)
        c.addView(timeText)
        c.addView(markerScroll)
        c.addView(transport)
        c.addView(markerTools)
        c.addView(header("Tempo y tono"))
        c.addView(transposerHint)
        c.addView(tempoRow)
        c.addView(toneRow)
        c.addView(saveTempoBtn)
        c.addView(header("Volumen y paso de piezas"))
        c.addView(volumeText)
        c.addView(volumeBar)
        c.addView(endRow)
        c.addView(autoCb)
        c.addView(gapRow)
        c.addView(repeatCb)
        c.addView(header("Letras en la tele"))
        c.addView(lyricsCb)
        c.addView(tvStatusText)
        c.addView(lyricPreview)
        c.addView(lyricsManual)
        c.addView(lyricsTools)
        c.addView(markingPanel)
        c.addView(adelantoRow)
        c.addView(header("Combinaciones del órgano"))
        c.addView(pistons)
        c.addView(header("Registros que suenan"))
        c.addView(stopsGrid)
        c.addView(otherStopsText)
        c.addView(cleanCb)
        c.addView(header("Grabar"))
        c.addView(recordBtn)
        c.addView(recordText)
        c.addView(hint)
        val scroll = ScrollView(this).apply { addView(c) }

        val wide = resources.configuration.smallestScreenWidthDp >= 600
        val content = LinearLayout(this).apply {
            orientation = if (wide) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        }
        if (wide) {
            content.addView(listPanel, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f))
            content.addView(scroll, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.4f))
        } else {
            content.addView(listPanel, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
            content.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.3f))
        }
        root.addView(content, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    private inner class RowsAdapter : BaseAdapter() {
        override fun getCount(): Int = rows.size
        override fun getItem(position: Int): Any = rows[position]
        override fun getItemId(position: Int): Long = position.toLong()
        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val tv = (convertView as? TextView) ?: TextView(this@MainActivity).apply {
                textSize = 19f
                setPadding(dp(14), dp(13), dp(14), dp(13))
            }
            val e = rows[position]
            tv.text = when {
                e.isUp -> "⬆  Subir a la carpeta anterior"
                e.isDir -> "📁  ${e.name}"
                else -> {
                    val m = if (!e.moment.isNullOrEmpty()) "${e.moment} — " else ""
                    val miss = if (e.missing) "  (no está en la carpeta)" else ""
                    "♪  $m${stripExt(e.name)}$miss"
                }
            }
            tv.setTextColor(if (e.missing) Color.GRAY else Color.BLACK)
            val current = playPieces.getOrNull(currentIndex)
            val upcoming = playPieces.getOrNull(nextIndex)
            tv.setBackgroundColor(
                when {
                    !e.isDir && current != null && e.docId == current.docId && e.moment == current.moment -> Color.argb(90, 63, 81, 181)
                    !e.isDir && upcoming != null && e.docId == upcoming.docId && e.moment == upcoming.moment -> Color.argb(70, 255, 152, 0)
                    else -> Color.TRANSPARENT
                }
            )
            return tv
        }
    }

    // ================================================================== refresco periódico

    private fun refreshControls() {
        val s = player.song
        titleText.text = s?.name ?: "Ninguna pieza cargada"
        nextText.text = nextLabel()
        val dur = player.durationUs
        val pos = player.positionUs
        val f = player.tempo
        if (!userSeeking) {
            timeText.text = "${fmt(pos / f)} / ${fmt(dur / f)}"
            seekBar.progress = if (dur > 0) (pos * 1000 / dur).toInt().coerceIn(0, 1000) else 0
        }
        playBtn.text = if (player.playing) "⏸ Pausa" else "▶ Reproducir"
        tempoBtn.text = "Tempo ${(f * 100).roundToInt()} %"
        val t = player.transpose
        toneBtn.text = when {
            t == 0 -> "Tono original"
            t > 0 -> "Tono +$t"
            else -> "Tono $t"
        }
        val le = player.loopEndUs
        val ls = player.loopStartUs
        loopBtn.text = if (le != null && ls != null) "Terminar repetición" else "Repetir tramo…"
        recordText.text = if (recorder.recording)
            "● Grabando ${fmt(recorder.elapsedSeconds * 1e6)} — ${recorder.count} mensajes recibidos"
        else ""
        refreshStops()
        refreshLyrics()
    }

    private fun nextLabel(): String {
        val ps = pendingStart
        if (ps != null) {
            val left = ((pendingStartAt - System.currentTimeMillis()) / 1000.0).roundToInt().coerceAtLeast(0)
            return "La siguiente pieza empieza en $left s"
        }
        val le = player.loopEndUs
        val ls = player.loopStartUs
        if (le != null && ls != null) {
            val a = markers.firstOrNull { it.posMs * 1000 == ls }?.name ?: fmt(ls)
            val b = markers.firstOrNull { it.posMs * 1000 == le }?.name ?: fmt(le)
            return "Repitiendo de «$a» a «$b» hasta pulsar «Terminar repetición»"
        }
        if (playPieces.isEmpty() || currentIndex < 0) return ""
        val n = followingIndex()
        return if (n >= 0) "Siguiente: ${stripExt(playPieces[n].name)}" else "Es la última pieza"
    }

    private fun followingIndex(): Int = when {
        nextIndex >= 0 -> nextIndex
        currentIndex + 1 < playPieces.size -> currentIndex + 1
        repeatCb.isChecked && playPieces.isNotEmpty() -> 0
        else -> -1
    }

    private fun refreshStops() {
        val on = shownStops.stopsOn().toHashSet()
        for ((key, tv) in stopViews) {
            val isOn = key in on
            tv.setBackgroundColor(if (isOn) Color.argb(110, 255, 193, 7) else Color.TRANSPARENT)
            tv.setTypeface(null, if (isOn) Typeface.BOLD else Typeface.NORMAL)
        }
        for ((ch, tv) in tremoloViews) {
            val trem = shownStops.tremoloOn(ch)
            tv.setBackgroundColor(if (trem) Color.argb(110, 255, 193, 7) else Color.TRANSPARENT)
            tv.setTypeface(null, if (trem) Typeface.BOLD else Typeface.NORMAL)
        }
        val others = on.filter { Stops.find(it) == null }
        otherStopsText.text = if (others.isEmpty()) "" else "Otros registros puestos: " +
            others.joinToString(", ") { Stops.unknownLabel(it) }
    }

    // ================================================================== reproducción

    private fun cancelPendingStart() {
        pendingStart?.let { ui.removeCallbacks(it) }
        pendingStart = null
    }

    private fun piecesOfRows(): List<Entry> = rows.filter { !it.isDir && !it.isUp && !it.missing }

    private fun playPause() {
        cancelPendingStart()
        when {
            player.playing -> player.pause()
            player.song != null -> player.play()
            else -> {
                val pieces = piecesOfRows()
                if (pieces.isEmpty()) toast("No hay piezas aquí")
                else loadPiece(0, true, pieces, folderStack.lastOrNull())
            }
        }
    }

    private fun previous() {
        if (player.positionUs > 3_000_000 || currentIndex <= 0) {
            player.seekTo(0.0)
        } else {
            loadPiece(currentIndex - 1, player.playing, playPieces, playDirId)
        }
    }

    private fun next() {
        if (playPieces.isEmpty()) return
        var n = if (nextIndex >= 0) nextIndex else currentIndex + 1
        if (n >= playPieces.size) {
            if (repeatCb.isChecked) n = 0 else { toast("Es la última pieza"); return }
        }
        loadPiece(n, player.playing, playPieces, playDirId)
    }

    private fun onPieceFinished() {
        val n = playPieces.size
        if (n == 0) return
        var nx = if (nextIndex >= 0) nextIndex else currentIndex + 1
        if (nx >= n) {
            if (repeatCb.isChecked) nx = 0 else {
                listAdapter.notifyDataSetChanged()
                return
            }
        }
        val auto = autoCb.isChecked
        // En Misa deja preparada la siguiente; con el templo abierto, la toca tras la pausa.
        loadPiece(nx, false, playPieces, playDirId) {
            if (auto) {
                val gap = prefs.getInt("gap", 3)
                val r = Runnable {
                    pendingStart = null
                    player.play()
                }
                pendingStart = r
                pendingStartAt = System.currentTimeMillis() + gap * 1000L
                ui.postDelayed(r, gap * 1000L)
            }
        }
    }

    private fun pieceKey(dirId: String?, name: String) = "pz:" + (dirId ?: "") + "/" + name

    private fun savedTempo(dirId: String?, name: String): Double {
        val v = prefs.getString(pieceKey(dirId, name), null) ?: return 1.0
        return v.substringBefore(';').toDoubleOrNull() ?: 1.0
    }

    private fun savedTone(dirId: String?, name: String): Int {
        val v = prefs.getString(pieceKey(dirId, name), null) ?: return 0
        return v.substringAfter(';', "0").toIntOrNull() ?: 0
    }

    private fun rememberPieceSettings() {
        val e = playPieces.getOrNull(currentIndex) ?: return
        val key = pieceKey(playDirId, e.name)
        if (player.tempo == 1.0 && player.transpose == 0) prefs.edit().remove(key).apply()
        else prefs.edit().putString(key, "${player.tempo};${player.transpose}").apply()
        durGenBump()
    }

    private fun setTempoPercent(p: Int) {
        player.tempo = p.coerceIn(25, 200) / 100.0
        rememberPieceSettings()
    }

    private fun setTone(t: Int) {
        player.setTranspose(t.coerceIn(-6, 6))
        rememberPieceSettings()
    }

    private fun loadPiece(index: Int, autoplay: Boolean, pieces: List<Entry>, dirId: String?, after: (() -> Unit)? = null) {
        val st = store ?: return
        if (index !in pieces.indices || dirId == null) return
        val e = pieces[index]
        Thread {
            try {
                val song = Smf.parse(st.read(e.docId), stripExt(e.name))
                val docs = st.listAll(dirId)
                val base = stripExt(e.name)
                val mDoc = docs.firstOrNull { it.name.equals("$base.markers", true) }
                val lDoc = docs.firstOrNull { it.name.equals("$base.letras", true) }
                val mk = mDoc?.let { Markers.parse(st.readText(it.docId)) } ?: ArrayList()
                val ly = lDoc?.let { Lyrics.parse(st.readText(it.docId)) }
                ui.post {
                    player.load(song)
                    player.tempo = savedTempo(dirId, e.name)
                    player.setTranspose(savedTone(dirId, e.name))
                    playPieces = pieces
                    playDirId = dirId
                    currentIndex = index
                    nextIndex = -1
                    markers = mk
                    lyrics = ly
                    manualText = null
                    lastAutoIndex = -999
                    if (markingMode) toggleMarking()
                    rebuildMarkerButtons()
                    if (autoplay) player.play()
                    after?.invoke()
                    listAdapter.notifyDataSetChanged()
                }
            } catch (ex: Exception) {
                ui.post { toast("No se pudo abrir ${e.name}: ${ex.message}") }
            }
        }.start()
    }

    private fun onRowClick(e: Entry, longPress: Boolean) {
        when {
            e.isUp -> {
                if (folderStack.size > 1) {
                    folderStack.removeAt(folderStack.size - 1)
                    folderNames.removeAt(folderNames.size - 1)
                    currentListName = null
                    refreshList()
                }
            }
            e.isDir -> {
                folderStack.add(e.docId)
                folderNames.add(e.name)
                currentListName = null
                refreshList()
            }
            e.missing -> toast("Esta pieza no está en la carpeta")
            else -> {
                val pieces = piecesOfRows()
                val idx = pieces.indexOfFirst { it.docId == e.docId && it.moment == e.moment }
                if (idx < 0) return
                val samePieces = playDirId == folderStack.lastOrNull() && playPieces == pieces
                if (longPress) {
                    cancelPendingStart()
                    loadPiece(idx, true, pieces, folderStack.lastOrNull())
                } else if (player.playing) {
                    if (samePieces && idx != currentIndex) {
                        nextIndex = idx
                        listAdapter.notifyDataSetChanged()
                        toast("Será la siguiente: ${stripExt(e.name)}")
                    } else if (!samePieces) {
                        toast("Está sonando una pieza de otra lista o carpeta. Mantén pulsado para tocar esta ya.")
                    }
                } else {
                    cancelPendingStart()
                    loadPiece(idx, false, pieces, folderStack.lastOrNull())
                }
            }
        }
    }

    private fun fadeOut() {
        cancelPendingStart()
        if (!player.playing) return
        player.fadeOut { }
    }

    private fun silence() {
        cancelPendingStart()
        player.stop()
        for (ch in 0 until 16) {
            link.send(cc(ch, 123, 0))
            link.send(cc(ch, 120, 0))
        }
    }

    private fun changeGap(d: Int) {
        val g = (prefs.getInt("gap", 3) + d).coerceIn(0, 15)
        prefs.edit().putInt("gap", g).apply()
        updateGapText()
    }

    private fun updateGapText() {
        gapText.text = "Pausa entre piezas: ${prefs.getInt("gap", 3)} s"
    }

    // ================================================================== combinaciones

    /** Combinaciones 1–5 del SL 300: Program Change 1–5 en el canal 16, sin Bank Select. */
    private fun combination(n: Int) {
        if (!link.isConnected) { toast("Primero conecta con el órgano"); return }
        link.send(byteArrayOf(0xCF.toByte(), n.toByte()))
    }

    // ================================================================== marcadores

    private fun rebuildMarkerButtons() {
        markerRow.removeAllViews()
        for (m in markers) {
            val b = button(m.name.ifEmpty { "·" }, 15f) { player.seekTo(m.posMs * 1000.0) }
            markerRow.addView(b, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
    }

    private fun saveMarkers() {
        val st = store ?: return
        val dir = playDirId ?: return
        val e = playPieces.getOrNull(currentIndex) ?: return
        val name = stripExt(e.name) + ".markers"
        val list = markers.toList()
        rebuildMarkerButtons()
        Thread {
            val ok = try {
                if (list.isEmpty()) st.delete(dir, name)
                else st.writeFile(dir, name, Markers.serialize(list).toByteArray(Charsets.UTF_8))
            } catch (ex: Exception) { false }
            if (!ok) ui.post { toast("No se pudieron guardar los marcadores") }
        }.start()
    }

    private fun addMarker() {
        if (player.song == null) { toast("Carga primero una pieza"); return }
        val posMs = player.positionUs / 1000.0
        askText("Nombre del marcador (en ${fmt(posMs * 1000)})", "Marcador ${markers.size + 1}") { name ->
            if (name.isEmpty()) return@askText
            markers.add(Marker(posMs, name))
            markers.sortBy { it.posMs }
            saveMarkers()
        }
    }

    private fun editMarkers() {
        if (player.song == null) { toast("Carga primero una pieza"); return }
        if (markers.isEmpty()) { toast("Esta pieza no tiene marcadores"); return }
        val labels = markers.map { "${fmt(it.posMs * 1000)}   ${it.name}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Marcadores")
            .setItems(labels) { _, w -> markerOptions(w) }
            .setNegativeButton("Cerrar", null)
            .show()
    }

    private fun markerOptions(i: Int) {
        val m = markers.getOrNull(i) ?: return
        val opts = arrayOf("Ir aquí", "Cambiar nombre", "Mover 1 segundo antes", "Mover 1 segundo después", "Borrar")
        AlertDialog.Builder(this)
            .setTitle(m.name)
            .setItems(opts) { _, w ->
                when (w) {
                    0 -> player.seekTo(m.posMs * 1000.0)
                    1 -> askText("Nuevo nombre", m.name) { n ->
                        if (n.isNotEmpty()) {
                            m.name = n
                            saveMarkers()
                        }
                    }
                    2 -> { m.posMs = (m.posMs - 1000).coerceAtLeast(0.0); markers.sortBy { it.posMs }; saveMarkers(); editMarkers() }
                    3 -> { m.posMs += 1000; markers.sortBy { it.posMs }; saveMarkers(); editMarkers() }
                    4 -> { markers.remove(m); saveMarkers() }
                    else -> {}
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun loopClicked() {
        if (player.loopEndUs != null) {
            player.clearLoop()
            toast("La pieza seguirá hasta el final")
            return
        }
        if (markers.size < 2) { toast("Para repetir un tramo hacen falta al menos dos marcadores"); return }
        val labels = markers.map { "${fmt(it.posMs * 1000)}   ${it.name}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Repetir desde…")
            .setItems(labels) { _, a ->
                val after = markers.drop(a + 1)
                if (after.isEmpty()) { toast("Elige un marcador que tenga otro después"); return@setItems }
                AlertDialog.Builder(this)
                    .setTitle("…hasta")
                    .setItems(after.map { "${fmt(it.posMs * 1000)}   ${it.name}" }.toTypedArray()) { _, b ->
                        player.setLoop(markers[a].posMs * 1000.0, after[b].posMs * 1000.0)
                        toast("Se repetirá hasta pulsar «Terminar repetición»")
                    }
                    .setNegativeButton("Cancelar", null)
                    .show()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ================================================================== guardar con tempo y tono

    private fun saveWithTempoTone() {
        val st = store ?: return
        val song = player.song ?: run { toast("Carga primero una pieza"); return }
        val e = playPieces.getOrNull(currentIndex) ?: return
        val dir = playDirId ?: return
        if (player.playing) { toast("Detén la pieza antes de guardar"); return }
        val f = player.tempo
        val t = player.transpose
        if (f == 1.0 && t == 0) { toast("La pieza ya está con su tempo y tono originales"); return }
        AlertDialog.Builder(this)
            .setTitle("Guardar con este tempo y tono")
            .setMessage("Se guardará «${stripExt(e.name)}» al ${(f * 100).roundToInt()} % y con el tono ${if (t > 0) "+$t" else "$t"}. El original se guarda en la carpeta «Originales».")
            .setPositiveButton("Guardar") { _, _ ->
                Thread {
                    val ok = try {
                        val original = st.read(e.docId)
                        val origDir = st.ensureDir(dir, FolderStore.ORIGINALS_DIR) ?: throw IllegalStateException("Originales")
                        if (st.find(origDir, e.name) == null) st.create(origDir, e.name, original)
                        val mName = stripExt(e.name) + ".markers"
                        if (markers.isNotEmpty() && st.find(origDir, mName) == null) {
                            st.writeFile(origDir, mName, Markers.serialize(markers).toByteArray(Charsets.UTF_8))
                        }
                        val events = ArrayList<Pair<Long, ByteArray>>()
                        for (i in 0 until song.size) {
                            var d = song.data[i]
                            val status = d[0].toInt() and 0xFF
                            val type = status and 0xF0
                            val ch = status and 0x0F
                            if ((type == 0x90 || type == 0x80) && d.size >= 3 && ch != 15 && t != 0) {
                                val k = (d[1].toInt() and 0x7F) + t
                                if (k < 0 || k > 127) continue
                                d = byteArrayOf(d[0], k.toByte(), d[2])
                            }
                            events.add(Pair((song.timeUs[i] / f * 960.0 / 1e6).roundToLong(), d))
                        }
                        st.writeFile(dir, e.name, Smf.write(events, song.name), "audio/midi")
                        val scaled = markers.map { Marker(it.posMs / f, it.name) }
                        if (scaled.isNotEmpty()) st.writeFile(dir, mName, Markers.serialize(scaled).toByteArray(Charsets.UTF_8))
                        true
                    } catch (ex: Exception) { false }
                    ui.post {
                        if (ok) {
                            prefs.edit().remove(pieceKey(dir, e.name)).apply()
                            durCache.remove(e.docId)
                            toast("Guardada. El original está en «Originales».")
                            loadPiece(currentIndex, false, playPieces, playDirId)
                            computeDurations()
                        } else toast("No se pudo guardar")
                    }
                }.start()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ================================================================== letras

    private fun adelantoSec(): Int = prefs.getInt("adelanto", 2)

    private fun changeAdelanto(d: Int) {
        prefs.edit().putInt("adelanto", (adelantoSec() + d).coerceIn(0, 6)).apply()
        updateAdelantoText()
    }

    private fun updateAdelantoText() {
        adelantoText.text = "La letra sale ${adelantoSec()} s antes"
    }

    private fun isLyricMarker(name: String): Boolean =
        Lyrics.isBlack(name) || Lyrics.isTitle(name) || (lyrics?.indexOf(name) ?: -1) >= 0

    private fun computeLyric(): String {
        val s = player.song ?: return ""
        var tMs = player.positionUs / 1000.0
        if (player.playing) tMs += adelantoSec() * 1000.0 * player.tempo
        var autoIdx = -1
        for ((i, m) in markers.withIndex()) {
            if (m.posMs <= tMs) {
                if (isLyricMarker(m.name)) autoIdx = i
            } else break
        }
        if (autoIdx != lastAutoIndex) {
            lastAutoIndex = autoIdx
            manualText = null
        }
        manualText?.let { return it }
        if (autoIdx < 0) { shownBlock = -1; return s.name }
        val name = markers[autoIdx].name
        if (Lyrics.isBlack(name)) { shownBlock = -1; return "" }
        if (Lyrics.isTitle(name)) { shownBlock = -1; return s.name }
        val ly = lyrics ?: return ""
        val bi = ly.indexOf(name)
        shownBlock = bi
        return if (bi >= 0) ly.blocks[bi].second else ""
    }

    private fun refreshLyrics() {
        val on = lyricsCb.isChecked
        val p = presentation
        tvStatusText.text = when {
            !on -> "Desactivado: la tele no se usa."
            p != null -> "Tele conectada como segunda pantalla (${p.display.name}). En la tablet sigues viendo los botones."
            fullDialog != null -> "Letras a pantalla completa en la tablet (para duplicar la pantalla en la tele)."
            else -> "No hay segunda pantalla. Conecta la tele con Smart View (duplicar pantalla) o, si usas Chromecast, pulsa «Pantalla completa»."
        }
        if (!on) { lyricPreview.text = ""; return }
        val text = computeLyric()
        p?.view?.show(text)
        fullView?.show(text)
        val first = text.lineSequence().firstOrNull { it.isNotBlank() } ?: ""
        lyricPreview.text = when {
            player.song == null -> ""
            text.isEmpty() -> "En la tele: (pantalla en negro)"
            lyrics == null && shownBlock < 0 && text == player.song?.name -> "En la tele: título — esta pieza no tiene letra todavía"
            else -> "En la tele: $first…"
        }
    }

    private fun manualStep(d: Int) {
        val ly = lyrics ?: run { toast("Esta pieza no tiene letra"); return }
        if (ly.blocks.isEmpty()) return
        val i = (shownBlock + d).coerceIn(0, ly.blocks.size - 1)
        shownBlock = i
        manualText = ly.blocks[i].second
    }

    private fun manualBlack() {
        shownBlock = -1
        manualText = ""
    }

    private fun manualTitle() {
        shownBlock = -1
        manualText = player.song?.name ?: ""
    }

    private fun updatePresentation() {
        val on = lyricsCb.isChecked
        val dm = getSystemService(DISPLAY_SERVICE) as DisplayManager
        val display = if (on) dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).firstOrNull() else null
        val current = presentation
        if (display == null) {
            if (current != null) {
                try { current.dismiss() } catch (e: Exception) { }
                presentation = null
            }
            return
        }
        if (current != null && current.display.displayId == display.displayId) return
        try { current?.dismiss() } catch (e: Exception) { }
        val p = TvPresentation(this, display)
        try {
            p.show()
            presentation = p
            closeFullScreen()
        } catch (e: Exception) {
            presentation = null
        }
    }

    private fun openFullScreen() {
        if (!lyricsCb.isChecked) lyricsCb.isChecked = true
        if (fullDialog != null) return
        val d = Dialog(this, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
        val v = LyricsView(this)
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.rgb(20, 20, 20))
        }
        fun small(text: String, onClick: () -> Unit) = Button(this).apply {
            this.text = text
            textSize = 14f
            isAllCaps = false
            setTextColor(Color.LTGRAY)
            setBackgroundColor(Color.rgb(35, 35, 35))
            setOnClickListener { onClick() }
        }
        for (b in listOf(
            small("◀ Letra") { manualStep(-1) },
            small("Letra ▶") { manualStep(1) },
            small("Negro") { manualBlack() },
            small("⏯") { playPause() },
            small("Salir") { closeFullScreen() }
        )) {
            bar.addView(b, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(2, 2, 2, 2) })
        }
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.BLACK)
            addView(v, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
            addView(bar)
        }
        d.setContentView(layout)
        d.setOnDismissListener {
            fullDialog = null
            fullView = null
        }
        d.window?.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        fullDialog = d
        fullView = v
        d.show()
    }

    private fun closeFullScreen() {
        try { fullDialog?.dismiss() } catch (e: Exception) { }
        fullDialog = null
        fullView = null
    }

    private fun editLyrics() {
        val st = store ?: return
        val dir = playDirId
        val e = playPieces.getOrNull(currentIndex)
        if (player.song == null || dir == null || e == null) { toast("Carga primero una pieza"); return }
        val name = stripExt(e.name) + ".letras"
        Thread {
            val existing = try { st.find(dir, name)?.let { st.readText(it.docId) } } catch (ex: Exception) { null }
            ui.post {
                val et = EditText(this).apply {
                    setText(existing ?: Lyrics.TEMPLATE)
                    inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
                    minLines = 12
                    gravity = Gravity.TOP or Gravity.START
                    textSize = 16f
                }
                val help = label(
                    "Escribe cada parte con su nombre entre corchetes: [Estribillo], [Estrofa 1]… " +
                        "Luego, con «Marcar letras», pulsa cada parte cuando empiece mientras suena la pieza.", 13f
                )
                val box = LinearLayout(this).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(dp(16), dp(8), dp(16), 0)
                    addView(help)
                    addView(et)
                }
                AlertDialog.Builder(this)
                    .setTitle("Letra de «${stripExt(e.name)}»")
                    .setView(ScrollView(this).apply { addView(box) })
                    .setPositiveButton("Guardar") { _, _ ->
                        val text = et.text.toString()
                        lyrics = Lyrics.parse(text)
                        manualText = null
                        lastAutoIndex = -999
                        if (markingMode) buildMarkingPanel()
                        Thread {
                            val ok = try { st.writeFile(dir, name, text.toByteArray(Charsets.UTF_8)) } catch (ex: Exception) { false }
                            ui.post { toast(if (ok) "Letra guardada" else "No se pudo guardar la letra") }
                        }.start()
                    }
                    .setNegativeButton("Cancelar", null)
                    .show()
            }
        }.start()
    }

    private fun toggleMarking() {
        if (!markingMode) {
            val ly = lyrics
            if (player.song == null) { toast("Carga primero una pieza"); return }
            if (ly == null || ly.blocks.isEmpty()) { toast("Escribe primero la letra de esta pieza"); return }
            markingMode = true
            buildMarkingPanel()
            markingPanel.visibility = View.VISIBLE
            markBtn.text = "Terminar de marcar"
        } else {
            markingMode = false
            markingPanel.visibility = View.GONE
            markBtn.text = "Marcar letras"
        }
    }

    private fun buildMarkingPanel() {
        markingPanel.removeAllViews()
        val ly = lyrics ?: return
        markingPanel.addView(label(
            "Pon la pieza y pulsa cada parte justo cuando empiece. Se crea ahí un marcador con su nombre. " +
                "Si te equivocas, corrígelo en «Marcadores…».", 14f
        ))
        val names = ly.blocks.map { it.first }.distinct() + "Negro"
        var line: LinearLayout? = null
        for ((i, n) in names.withIndex()) {
            if (i % 3 == 0) {
                line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                markingPanel.addView(line)
            }
            line?.addView(button(n, 17f) { markNow(n) }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        val rest = (3 - names.size % 3) % 3
        for (k in 0 until rest) line?.addView(View(this), LinearLayout.LayoutParams(0, 1, 1f))
    }

    private fun markNow(name: String) {
        if (player.song == null) return
        val posMs = player.positionUs / 1000.0
        markers.add(Marker(posMs, name))
        markers.sortBy { it.posMs }
        saveMarkers()
        Toast.makeText(this, "«$name» en ${fmt(posMs * 1000)}", Toast.LENGTH_SHORT).show()
    }

    // ================================================================== grabación

    private fun toggleRecord() {
        if (!recorder.recording) {
            if (!link.isConnected) {
                toast("Primero conecta con el órgano")
                return
            }
            recorder.start()
            recordBtn.text = "■ Parar y guardar la grabación"
        } else {
            val events = recorder.stop()
            recordBtn.text = "● Grabar"
            if (events == null) {
                toast("No ha llegado nada del órgano. Comprueba que el MIDI OUT del órgano esté conectado.")
                return
            }
            try {
                File(filesDir, "ultima_grabacion.mid").writeBytes(Smf.write(events, "Última grabación"))
            } catch (e: Exception) { }
            askNameAndSave(events)
        }
    }

    private fun askNameAndSave(events: List<Pair<Long, ByteArray>>) {
        val default = "Grabación " + SimpleDateFormat("yyyy-MM-dd HH.mm", Locale.ROOT).format(Date())
        val et = EditText(this).apply {
            setText(default)
            selectAll()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        }
        AlertDialog.Builder(this)
            .setTitle("Nombre de la grabación")
            .setView(et)
            .setCancelable(false)
            .setPositiveButton("Guardar") { _, _ ->
                saveRecording(et.text.toString().trim().ifEmpty { default }, events)
            }
            .setNegativeButton("Descartar") { _, _ ->
                AlertDialog.Builder(this)
                    .setTitle("¿Descartar la grabación?")
                    .setMessage("Se perderá lo que acabas de grabar.")
                    .setPositiveButton("Descartar") { _, _ -> }
                    .setNegativeButton("No, guardarla") { _, _ -> askNameAndSave(events) }
                    .setCancelable(false)
                    .show()
            }
            .show()
    }

    private fun saveRecording(name: String, events: List<Pair<Long, ByteArray>>) {
        val st = store
        if (st == null) {
            pendingEvents = events
            pendingName = name
            toast("Elige la carpeta donde guardar las grabaciones")
            pickFolder()
            return
        }
        val clean = GoList.cleanName(name)
        val fileName = if (clean.lowercase(Locale.ROOT).endsWith(".mid")) clean else "$clean.mid"
        val bytes = Smf.write(events, stripExt(fileName))
        val dir = folderStack.lastOrNull() ?: st.rootId
        Thread {
            val ok = try { st.create(dir, fileName, bytes) } catch (e: Exception) { false }
            ui.post {
                if (ok) {
                    toast("Guardada: ${stripExt(fileName)}")
                    refreshList()
                } else {
                    toast("No se pudo guardar. Prueba con otro nombre.")
                    askNameAndSave(events)
                }
            }
        }.start()
    }

    // ================================================================== carpeta y listas

    private fun pickFolder() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
            addFlags(
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
            )
            try {
                putExtra(
                    DocumentsContract.EXTRA_INITIAL_URI,
                    Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADocuments")
                )
            } catch (e: Exception) { }
        }
        try {
            @Suppress("DEPRECATION")
            startActivityForResult(i, REQ_FOLDER)
        } catch (e: Exception) {
            toast("No se puede abrir el selector de carpetas")
        }
    }

    @Deprecated("API de Activity")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_FOLDER || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            contentResolver.takePersistableUriPermission(
                uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
        } catch (e: Exception) { }
        prefs.edit().putString("tree", uri.toString()).apply()
        openStore(uri)
        val ev = pendingEvents
        val nm = pendingName
        if (ev != null && nm != null) {
            pendingEvents = null
            pendingName = null
            saveRecording(nm, ev)
        }
    }

    private fun restoreFolder() {
        val s = prefs.getString("tree", null) ?: return
        val uri = Uri.parse(s)
        val ok = contentResolver.persistedUriPermissions.any { it.uri == uri && it.isReadPermission }
        if (ok) openStore(uri) else folderText.text = "Vuelve a elegir la carpeta con el botón «Carpeta»"
    }

    private fun openStore(uri: Uri) {
        try {
            val st = FolderStore(this, uri)
            store = st
            folderStack.clear()
            folderNames.clear()
            folderStack.add(st.rootId)
            folderNames.add(st.nameOf(st.rootId).ifEmpty { "Carpeta" })
            currentListName = null
            refreshList()
        } catch (e: Exception) {
            toast("No se puede abrir la carpeta: ${e.message}")
        }
    }

    private fun refreshList() {
        val st = store ?: return
        val dir = folderStack.lastOrNull() ?: return
        val result = ArrayList<Entry>()
        try {
            folderDocs = st.listAll(dir)
            val listsDir = folderDocs.firstOrNull { it.isDir && it.name.equals(FolderStore.LISTS_DIR, true) }
            listsDirId = listsDir?.docId
            listDocs = if (listsDir != null) st.listAll(listsDir.docId)
                .filter { !it.isDir && it.name.endsWith(".golist", true) }
                .sortedByDescending { it.modified } else emptyList()
            val listName = currentListName
            val listDoc = if (listName != null) listDocs.firstOrNull { stripExt(it.name) == listName } else null
            if (listDoc == null) {
                currentListName = null
                if (folderStack.size > 1) result.add(Entry("", "..", isDir = true, isUp = true))
                for (d in st.sortByName(folderDocs)) {
                    if (d.name.startsWith(".")) continue
                    if (d.isDir) {
                        if (!d.name.equals(FolderStore.LISTS_DIR, true)) result.add(Entry(d.docId, d.name, true))
                    } else if (FolderStore.isMidi(d.name)) {
                        result.add(Entry(d.docId, d.name, false))
                    }
                }
            } else {
                val gl = GoList.parse(st.readText(listDoc.docId))
                for ((moment, file) in gl.items) {
                    val doc = folderDocs.firstOrNull { !it.isDir && it.name.equals(file, true) }
                    result.add(Entry(doc?.docId ?: "", doc?.name ?: file, false, moment = if (gl.isMass) moment else null, missing = doc == null))
                }
            }
        } catch (e: Exception) {
            toast("No se puede leer la carpeta: ${e.message}")
        }
        rows = result
        folderText.text = "Carpeta: " + folderNames.joinToString(" / ")
        listBtn.text = "Lista: " + (currentListName ?: "todas las piezas de la carpeta") + " ▾"
        listAdapter.notifyDataSetChanged()
        computeDurations()
    }

    private fun durGenBump() {
        computeDurations()
    }

    private fun computeDurations() {
        val st = store ?: return
        val dir = folderStack.lastOrNull() ?: return
        val pieces = piecesOfRows()
        val gen = ++durGen
        if (pieces.isEmpty()) { durationText.text = ""; return }
        durationText.text = "${pieces.size} piezas · calculando duración…"
        Thread {
            var total = 0.0
            for (p in pieces) {
                if (gen != durGen) return@Thread
                val us = durCache[p.docId] ?: try {
                    Smf.parse(st.read(p.docId), "").durationUs.also { durCache[p.docId] = it }
                } catch (e: Exception) { 0L }
                total += us / savedTempo(dir, p.name)
            }
            ui.post {
                if (gen == durGen) durationText.text = "${pieces.size} piezas · duración total ${fmt(total)}"
            }
        }.start()
    }

    private fun chooseList() {
        if (store == null) { toast("Elige primero una carpeta"); return }
        val names = listOf("Todas las piezas de la carpeta") + listDocs.map { stripExt(it.name) }
        AlertDialog.Builder(this)
            .setTitle("Lista")
            .setItems(names.toTypedArray()) { _, w ->
                currentListName = if (w == 0) null else names[w]
                refreshList()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun deleteList() {
        val st = store ?: return
        val name = currentListName ?: run { toast("Elige primero una lista"); return }
        val dirId = listsDirId ?: return
        AlertDialog.Builder(this)
            .setTitle("¿Borrar la lista «$name»?")
            .setMessage("Solo se borra la lista; las piezas siguen en la carpeta.")
            .setPositiveButton("Borrar") { _, _ ->
                Thread {
                    val ok = st.delete(dirId, "$name.golist")
                    ui.post {
                        if (!ok) toast("No se pudo borrar")
                        currentListName = null
                        refreshList()
                    }
                }.start()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun editList(existingName: String?) {
        val st = store ?: run { toast("Elige primero una carpeta"); return }
        val dir = folderStack.lastOrNull() ?: return
        val working: GoList = if (existingName != null) {
            val doc = listDocs.firstOrNull { stripExt(it.name) == existingName }
            (try { doc?.let { GoList.parse(st.readText(it.docId)) } } catch (e: Exception) { null }) ?: GoList(true, ArrayList())
        } else GoList(true, ArrayList())
        val midis = st.sortByName(folderDocs.filter { !it.isDir && FolderStore.isMidi(it.name) })

        val nameEt = EditText(this).apply {
            setText(existingName ?: "")
            hint = "Nombre de la lista (p. ej. Misa del domingo)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
        }
        val massRb = RadioButton(this).apply { text = "Misa (cada pieza con su momento)"; id = View.generateViewId() }
        val freeRb = RadioButton(this).apply { text = "Libre"; id = View.generateViewId() }
        val group = RadioGroup(this).apply {
            orientation = RadioGroup.HORIZONTAL
            addView(massRb)
            addView(freeRb)
            check(if (working.isMass) massRb.id else freeRb.id)
        }
        val labels = ArrayList<String>()
        val adapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, labels)
        fun refreshItems() {
            labels.clear()
            for ((m, f) in working.items) {
                labels.add(if (massRb.isChecked && m.isNotEmpty()) "$m — ${stripExt(f)}" else stripExt(f))
            }
            adapter.notifyDataSetChanged()
        }
        group.setOnCheckedChangeListener { _, _ -> refreshItems() }
        val itemsView = ListView(this).apply { this.adapter = adapter }

        fun chooseMoment(current: String?, onPick: (String) -> Unit) {
            val moments = GoList.MOMENTS
            val pre = moments.indexOf(current ?: "").let { if (it >= 0) it else 0 }
            AlertDialog.Builder(this)
                .setTitle("Momento de la Misa")
                .setSingleChoiceItems(moments.toTypedArray(), pre) { d, w ->
                    onPick(moments[w])
                    d.dismiss()
                }
                .setNegativeButton("Cancelar", null)
                .show()
        }

        val addBtn = button("Añadir pieza", 16f) {
            if (midis.isEmpty()) { toast("No hay piezas en esta carpeta"); return@button }
            AlertDialog.Builder(this)
                .setTitle("Añadir pieza")
                .setItems(midis.map { stripExt(it.name) }.toTypedArray()) { _, w ->
                    val file = midis[w].name
                    if (massRb.isChecked) {
                        val proposed = GoList.proposeNext(working.items.lastOrNull()?.first)
                        chooseMoment(proposed) { m ->
                            working.items.add(Pair(m, file))
                            refreshItems()
                        }
                    } else {
                        working.items.add(Pair("", file))
                        refreshItems()
                    }
                }
                .setNegativeButton("Cancelar", null)
                .show()
        }
        itemsView.setOnItemClickListener { _, _, pos, _ ->
            val opts = arrayOf("Cambiar momento", "Subir", "Bajar", "Quitar de la lista")
            AlertDialog.Builder(this)
                .setTitle(labels.getOrNull(pos) ?: "")
                .setItems(opts) { _, w ->
                    when (w) {
                        0 -> {
                            if (massRb.isChecked) {
                                chooseMoment(working.items[pos].first) { m ->
                                    working.items[pos] = Pair(m, working.items[pos].second)
                                    refreshItems()
                                }
                            } else {
                                toast("Solo en las listas de Misa")
                            }
                        }
                        1 -> {
                            if (pos > 0) {
                                val it0 = working.items.removeAt(pos)
                                working.items.add(pos - 1, it0)
                                refreshItems()
                            }
                        }
                        2 -> {
                            if (pos < working.items.size - 1) {
                                val it0 = working.items.removeAt(pos)
                                working.items.add(pos + 1, it0)
                                refreshItems()
                            }
                        }
                        3 -> {
                            working.items.removeAt(pos)
                            refreshItems()
                        }
                        else -> {}
                    }
                }
                .show()
        }
        refreshItems()

        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(8), dp(16), 0)
            addView(nameEt)
            addView(group)
            addView(addBtn)
            addView(label("Toca una pieza para cambiar su momento, moverla o quitarla.", 13f))
            addView(itemsView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(320)))
        }
        AlertDialog.Builder(this)
            .setTitle(if (existingName == null) "Nueva lista" else "Editar lista")
            .setView(box)
            .setPositiveButton("Guardar") { _, _ ->
                val name = GoList.cleanName(nameEt.text.toString())
                if (name.isEmpty()) { toast("La lista necesita un nombre"); return@setPositiveButton }
                working.isMass = massRb.isChecked
                Thread {
                    val ok = try {
                        val ld = st.ensureDir(dir, FolderStore.LISTS_DIR) ?: throw IllegalStateException()
                        val written = st.writeFile(ld, "$name.golist", working.serialize().toByteArray(Charsets.UTF_8))
                        if (written && existingName != null && existingName != name) st.delete(ld, "$existingName.golist")
                        written
                    } catch (e: Exception) { false }
                    ui.post {
                        if (ok) {
                            currentListName = name
                            refreshList()
                        } else toast("No se pudo guardar la lista")
                    }
                }.start()
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    // ================================================================== conexión

    private fun btPermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun hasBtPermissions(): Boolean =
        btPermissions().all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    private fun withBt(action: () -> Unit) {
        if (hasBtPermissions()) action() else {
            pendingBtAction = action
            requestPermissions(btPermissions(), REQ_BT)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_BT) return
        val action = pendingBtAction
        pendingBtAction = null
        if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            action?.invoke()
        } else {
            toast("Sin el permiso de Bluetooth no puedo buscar el órgano")
        }
    }

    private fun btAdapter(): BluetoothAdapter? =
        (getSystemService(BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private fun onConnected() {
        disconnectedDialog?.dismiss()
        disconnectedDialog = null
        ui.removeCallbacks(reconnectRunnable)
        if (wasDisconnected) {
            wasDisconnected = false
            for (ch in 0 until 16) {
                link.send(cc(ch, 123, 0))
                link.send(cc(ch, 120, 0))
            }
            toast("Conexión recuperada. La pieza sigue en pausa: pulsa ▶ para continuar.")
        }
    }

    private fun onDisconnected() {
        if (link.userClosed) return
        cancelPendingStart()
        player.pause()
        wasDisconnected = true
        if (disconnectedDialog == null) {
            disconnectedDialog = AlertDialog.Builder(this)
                .setTitle("Se ha cortado la conexión con el órgano")
                .setMessage("La pieza se ha puesto en pausa. La tablet intenta reconectar sola…")
                .setPositiveButton("Entendido") { _, _ -> disconnectedDialog = null }
                .setCancelable(true)
                .setOnCancelListener { disconnectedDialog = null }
                .show()
        }
        reconnectAttempts = 0
        ui.removeCallbacks(reconnectRunnable)
        ui.postDelayed(reconnectRunnable, 3000)
    }

    private val reconnectRunnable = object : Runnable {
        override fun run() {
            if (link.isConnected || link.userClosed) return
            reconnectAttempts++
            autoConnect()
            if (reconnectAttempts < 40) ui.postDelayed(this, 6000)
            else statusText.text = "Sin conexión. Pulsa «Conectar»."
        }
    }

    private fun showConnectDialog() {
        val devices = link.available()
        val labels = ArrayList<String>()
        for (d in devices) {
            val kind = when (d.type) {
                MidiDeviceInfo.TYPE_BLUETOOTH -> "Bluetooth: "
                MidiDeviceInfo.TYPE_USB -> "USB: "
                else -> ""
            }
            labels.add(kind + link.nameOf(d))
        }
        labels.add("Buscar el órgano por Bluetooth…")
        if (link.isConnected) labels.add("Desconectar")
        AlertDialog.Builder(this)
            .setTitle("Conectar con el órgano")
            .setItems(labels.toTypedArray()) { _, which ->
                when {
                    which < devices.size -> {
                        val d = devices[which]
                        prefs.edit().putString("device", link.nameOf(d)).remove("ble").apply()
                        link.open(d)
                    }
                    which == devices.size -> withBt { scanBle() }
                    else -> {
                        link.close(true)
                        prefs.edit().remove("device").remove("ble").apply()
                        statusText.text = "Sin conectar"
                    }
                }
            }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    @SuppressLint("MissingPermission")
    private fun scanBle() {
        val adapter = btAdapter()
        if (adapter == null) { toast("Esta tablet no tiene Bluetooth"); return }
        if (!adapter.isEnabled) { toast("Activa el Bluetooth de la tablet"); return }
        val scanner = adapter.bluetoothLeScanner ?: run { toast("No se puede buscar por Bluetooth"); return }
        val found = LinkedHashMap<String, Pair<BluetoothDevice, String>>()
        val names = ArrayList<String>()
        val adapterView = ArrayAdapter(this, android.R.layout.simple_list_item_1, names)
        var stopped = false
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                runOnUiThread {
                    val d = result.device
                    if (stopped || found.containsKey(d.address)) return@runOnUiThread
                    val name = result.scanRecord?.deviceName ?: d.name ?: d.address
                    found[d.address] = Pair(d, name)
                    names.add(name)
                    adapterView.notifyDataSetChanged()
                }
            }
            override fun onScanFailed(errorCode: Int) {
                runOnUiThread { toast("Fallo al buscar por Bluetooth ($errorCode)") }
            }
        }
        fun stopScan() {
            if (stopped) return
            stopped = true
            try { scanner.stopScan(callback) } catch (e: Exception) { }
        }
        AlertDialog.Builder(this)
            .setTitle("Buscando el órgano por Bluetooth…")
            .setAdapter(adapterView) { _, which ->
                stopScan()
                val (dev, name) = found.values.elementAt(which)
                prefs.edit().putString("ble", dev.address).putString("bleName", name).remove("device").apply()
                link.openBle(dev, name)
            }
            .setNegativeButton("Cancelar") { _, _ -> stopScan() }
            .setOnDismissListener { stopScan() }
            .show()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(MIDI_SERVICE_UUID)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try {
            scanner.startScan(listOf(filter), settings, callback)
        } catch (e: Exception) {
            toast("No se puede buscar por Bluetooth: ${e.message}")
        }
        ui.postDelayed({ stopScan() }, 20_000)
    }

    @SuppressLint("MissingPermission")
    private fun autoConnect() {
        val device = prefs.getString("device", null)
        val ble = prefs.getString("ble", null)
        if (device != null) {
            link.available().firstOrNull { link.nameOf(it) == device }?.let { link.open(it) }
            return
        }
        if (ble != null && hasBtPermissions()) {
            val adapter = btAdapter() ?: return
            if (!adapter.isEnabled) {
                statusText.text = "El Bluetooth está apagado"
                return
            }
            try {
                val dev = adapter.getRemoteDevice(ble)
                link.openBle(dev, prefs.getString("bleName", null) ?: "el órgano")
            } catch (e: Exception) { }
        }
    }

    private fun autoConnectUsb(info: MidiDeviceInfo) {
        if (link.isConnected) return
        val device = prefs.getString("device", null) ?: return
        if (link.nameOf(info) == device) link.open(info)
    }
}
