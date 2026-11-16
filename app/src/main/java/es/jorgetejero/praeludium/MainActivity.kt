package es.jorgetejero.praeludium

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
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
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

class MainActivity : Activity() {

    companion object {
        private const val REQ_FOLDER = 1
        private const val REQ_BT = 2
        private val MIDI_SERVICE_UUID: UUID = UUID.fromString("03B80E5A-EDE8-4B33-A751-6CE34EC4C700")
    }

    private lateinit var prefs: SharedPreferences
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var link: MidiLink
    private lateinit var player: Player
    private val recorder = Recorder()

    private var store: FolderStore? = null
    private val folderStack = ArrayList<String>()
    private val folderNames = ArrayList<String>()
    private var entries: List<Entry> = emptyList()

    private var playPieces: List<Entry> = emptyList()
    private var playDirId: String? = null
    private var currentIndex = -1
    private var nextIndex = -1

    private var userSeeking = false
    private var pendingBtAction: (() -> Unit)? = null
    private var pendingEvents: List<Pair<Long, ByteArray>>? = null
    private var pendingName: String? = null

    // Vistas
    private lateinit var statusText: TextView
    private lateinit var folderText: TextView
    private lateinit var listView: ListView
    private lateinit var listAdapter: PiecesAdapter
    private lateinit var titleText: TextView
    private lateinit var nextText: TextView
    private lateinit var timeText: TextView
    private lateinit var seekBar: SeekBar
    private lateinit var playBtn: Button
    private lateinit var tempoBtn: Button
    private lateinit var toneBtn: Button
    private lateinit var volumeText: TextView
    private lateinit var volumeBar: SeekBar
    private lateinit var autoCb: CheckBox
    private lateinit var repeatCb: CheckBox
    private lateinit var recordBtn: Button
    private lateinit var recordText: TextView

    // ------------------------------------------------------------------ ciclo de vida

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = getSharedPreferences("ajustes", MODE_PRIVATE)
        player = Player { link.send(it) }
        player.onFinished = { ui.post { onPieceFinished() } }
        buildUi()
        link = MidiLink(
            this,
            { d, o, c, t -> recorder.feed(d, o, c, t) },
            { s -> ui.post { statusText.text = s } },
            { info -> ui.post { autoConnectUsb(info) } }
        )
        restoreFolder()
        autoConnect()
        ui.post(ticker)
    }

    override fun onDestroy() {
        ui.removeCallbacks(ticker)
        player.stop()
        link.close()
        super.onDestroy()
    }

    @Deprecated("Se mantiene para Android 10–12")
    override fun onBackPressed() {
        // Volver atrás no cierra la app, para no cortar la música.
        moveTaskToBack(true)
    }

    private val ticker = object : Runnable {
        override fun run() {
            refreshControls()
            ui.postDelayed(this, 200)
        }
    }

    // ------------------------------------------------------------------ interfaz

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

    private fun button(text: String, onClick: () -> Unit): Button = Button(this).apply {
        this.text = text
        textSize = 19f
        isAllCaps = false
        minHeight = dp(60)
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

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), dp(4), dp(8), dp(4))
        }

        statusText = label("Sin conectar", 17f).apply { setTypeface(typeface, Typeface.BOLD) }
        val connectBtn = button("Conectar") { showConnectDialog() }
        val folderBtn = button("Carpeta") { pickFolder() }
        val top = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(statusText, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 2f))
            addView(connectBtn, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(folderBtn, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        root.addView(top)

        // Lista de piezas
        folderText = label("Elige una carpeta con el botón «Carpeta»", 15f)
        listView = ListView(this)
        listAdapter = PiecesAdapter()
        listView.adapter = listAdapter
        listView.setOnItemClickListener { _, _, pos, _ -> onEntryClick(entries[pos], false) }
        listView.setOnItemLongClickListener { _, _, pos, _ -> onEntryClick(entries[pos], true); true }
        val listPanel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(folderText)
            addView(listView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        }

        // Controles
        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), 0, dp(4), dp(16))
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
        playBtn = button("▶ Reproducir") { playPause() }
        val transport = row(
            button("⏮ Anterior") { previous() },
            playBtn,
            button("⏹ Detener") { player.stop() },
            button("Siguiente ⏭") { next() }
        )
        tempoBtn = button("Tempo 100 %") { player.tempo = 1.0 }
        val tempoRow = row(
            button("Más lento") { changeTempo(-5) },
            tempoBtn,
            button("Más rápido") { changeTempo(5) }
        )
        toneBtn = button("Tono original") { player.setTranspose(0) }
        val toneRow = row(
            button("Bajar ½ tono") { changeTone(-1) },
            toneBtn,
            button("Subir ½ tono") { changeTone(1) }
        )
        volumeText = label("Volumen general del órgano: 127", 16f)
        volumeBar = SeekBar(this).apply {
            max = 127
            progress = 127
            setPadding(dp(16), dp(12), dp(16), dp(12))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) {
                    volumeText.text = "Volumen general del órgano: $p"
                    if (fromUser) {
                        player.mainVolume = p
                        link.send(cc(15, 7, p))
                    }
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) {}
            })
        }
        autoCb = CheckBox(this).apply {
            text = "Pasar automáticamente a la siguiente pieza (templo abierto)"
            textSize = 16f
            isChecked = prefs.getBoolean("auto", false)
            setOnCheckedChangeListener { _, c -> prefs.edit().putBoolean("auto", c).apply() }
        }
        repeatCb = CheckBox(this).apply {
            text = "Repetir la lista al terminar la última pieza"
            textSize = 16f
            isChecked = prefs.getBoolean("repeat", false)
            setOnCheckedChangeListener { _, c -> prefs.edit().putBoolean("repeat", c).apply() }
        }
        val endRow = row(
            button("Terminar suavemente") { fadeOut() },
            button("Silencio") { silence() }
        )
        recordBtn = button("● Grabar") { toggleRecord() }.apply { setTextColor(Color.rgb(180, 0, 0)) }
        recordText = label("", 16f)
        val hint = label(
            "Al grabar: pulsa Grabar y después pon la registración o la combinación, " +
                "para que quede guardada en la grabación.", 14f
        ).apply { setTextColor(Color.GRAY) }

        controls.addView(titleText)
        controls.addView(nextText)
        controls.addView(seekBar)
        controls.addView(timeText)
        controls.addView(transport)
        controls.addView(tempoRow)
        controls.addView(toneRow)
        controls.addView(volumeText)
        controls.addView(volumeBar)
        controls.addView(autoCb)
        controls.addView(repeatCb)
        controls.addView(endRow)
        controls.addView(View(this), LinearLayout.LayoutParams(1, dp(16)))
        controls.addView(recordBtn)
        controls.addView(recordText)
        controls.addView(hint)
        val scroll = ScrollView(this).apply { addView(controls) }

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

    private inner class PiecesAdapter : BaseAdapter() {
        override fun getCount(): Int = entries.size
        override fun getItem(position: Int): Any = entries[position]
        override fun getItemId(position: Int): Long = position.toLong()
        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val tv = (convertView as? TextView) ?: TextView(this@MainActivity).apply {
                textSize = 20f
                setPadding(dp(14), dp(14), dp(14), dp(14))
            }
            val e = entries[position]
            tv.text = when {
                e.isUp -> "⬆  Subir a la carpeta anterior"
                e.isDir -> "📁  ${e.name}"
                else -> "♪  ${stripExt(e.name)}"
            }
            val current = playPieces.getOrNull(currentIndex)?.docId
            val upcoming = playPieces.getOrNull(nextIndex)?.docId
            tv.setBackgroundColor(
                when {
                    !e.isDir && e.docId == current -> Color.argb(90, 63, 81, 181)
                    !e.isDir && e.docId == upcoming -> Color.argb(70, 255, 152, 0)
                    else -> Color.TRANSPARENT
                }
            )
            return tv
        }
    }

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
        tempoBtn.text = "Tempo ${Math.round(f * 100)} %"
        val t = player.transpose
        toneBtn.text = when {
            t == 0 -> "Tono original"
            t > 0 -> "Tono +$t"
            else -> "Tono $t"
        }
        recordText.text = if (recorder.recording)
            "● Grabando ${fmt(recorder.elapsedSeconds * 1e6)} — ${recorder.count} mensajes recibidos"
        else ""
    }

    private fun nextLabel(): String {
        if (playPieces.isEmpty() || currentIndex < 0) return ""
        val n = when {
            nextIndex >= 0 -> nextIndex
            currentIndex + 1 < playPieces.size -> currentIndex + 1
            repeatCb.isChecked -> 0
            else -> -1
        }
        return if (n >= 0) "Siguiente: ${stripExt(playPieces[n].name)}" else "Es la última pieza de la carpeta"
    }

    private fun fmt(us: Double): String {
        val secs = (us / 1e6).toInt().coerceAtLeast(0)
        return String.format(Locale.ROOT, "%d:%02d", secs / 60, secs % 60)
    }

    private fun stripExt(name: String): String {
        val i = name.lastIndexOf('.')
        return if (i > 0) name.substring(0, i) else name
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    private fun cc(ch: Int, c: Int, v: Int) = byteArrayOf((0xB0 or ch).toByte(), c.toByte(), v.toByte())

    // ------------------------------------------------------------------ reproducción

    private fun playPause() {
        when {
            player.playing -> player.pause()
            player.song != null -> player.play()
            else -> {
                val pieces = entries.filter { !it.isDir && !it.isUp }
                if (pieces.isEmpty()) toast("No hay piezas en esta carpeta")
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

    private fun changeTempo(stepPercent: Int) {
        val p = (Math.round(player.tempo * 100).toInt() + stepPercent).coerceIn(25, 200)
        player.tempo = p / 100.0
    }

    private fun changeTone(step: Int) {
        player.setTranspose((player.transpose + step).coerceIn(-6, 6))
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
        // En Misa deja preparada la siguiente; con el templo abierto, la toca.
        loadPiece(nx, autoCb.isChecked, playPieces, playDirId)
    }

    private fun loadPiece(index: Int, autoplay: Boolean, pieces: List<Entry>, dirId: String?) {
        val st = store ?: return
        if (index !in pieces.indices) return
        val e = pieces[index]
        Thread {
            try {
                val song = Smf.parse(st.read(e.docId), stripExt(e.name))
                ui.post {
                    player.load(song)
                    playPieces = pieces
                    playDirId = dirId
                    currentIndex = index
                    nextIndex = -1
                    if (autoplay) player.play()
                    listAdapter.notifyDataSetChanged()
                }
            } catch (ex: Exception) {
                ui.post { toast("No se pudo abrir ${e.name}: ${ex.message}") }
            }
        }.start()
    }

    private fun onEntryClick(e: Entry, longPress: Boolean) {
        when {
            e.isUp -> {
                if (folderStack.size > 1) {
                    folderStack.removeAt(folderStack.size - 1)
                    folderNames.removeAt(folderNames.size - 1)
                    refreshList()
                }
            }
            e.isDir -> {
                folderStack.add(e.docId)
                folderNames.add(e.name)
                refreshList()
            }
            else -> {
                val pieces = entries.filter { !it.isDir && !it.isUp }
                val idx = pieces.indexOfFirst { it.docId == e.docId }
                if (idx < 0) return
                val sameFolder = playDirId == folderStack.lastOrNull()
                if (longPress) {
                    loadPiece(idx, true, pieces, folderStack.lastOrNull())
                } else if (player.playing) {
                    if (sameFolder && idx != currentIndex) {
                        nextIndex = idx
                        listAdapter.notifyDataSetChanged()
                        toast("Será la siguiente: ${stripExt(e.name)}")
                    } else if (!sameFolder) {
                        toast("Está sonando una pieza de otra carpeta. Mantén pulsado para tocar esta ya.")
                    }
                } else {
                    loadPiece(idx, false, pieces, folderStack.lastOrNull())
                }
            }
        }
    }

    private fun fadeOut() {
        if (!player.playing) return
        Thread {
            player.fading = true
            val v0 = player.mainVolume
            for (i in 1..50) {
                link.send(cc(15, 7, v0 * (50 - i) / 50))
                Thread.sleep(100)
            }
            player.stop()
            Thread.sleep(1500)
            link.send(cc(15, 7, v0))
            player.fading = false
        }.start()
    }

    private fun silence() {
        player.stop()
        for (ch in 0 until 16) {
            link.send(cc(ch, 123, 0))
            link.send(cc(ch, 120, 0))
        }
    }

    // ------------------------------------------------------------------ grabación

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
                toast("No ha llegado nada del órgano. Comprueba que el WIDI esté enchufado también en el MIDI OUT del órgano.")
                return
            }
            // Copia de seguridad por si algo falla al guardar.
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
        val clean = name.replace(Regex("[\\\\/:*?\"<>|]"), "-")
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

    // ------------------------------------------------------------------ carpeta

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
            refreshList()
        } catch (e: Exception) {
            toast("No se puede abrir la carpeta: ${e.message}")
        }
    }

    private fun refreshList() {
        val st = store ?: return
        val dir = folderStack.lastOrNull() ?: return
        val list = ArrayList<Entry>()
        if (folderStack.size > 1) list.add(Entry("", "..", isDir = true, isUp = true))
        try {
            list.addAll(st.list(dir))
        } catch (e: Exception) {
            toast("No se puede leer la carpeta: ${e.message}")
        }
        entries = list
        folderText.text = "Carpeta: " + folderNames.joinToString(" / ")
        listAdapter.notifyDataSetChanged()
    }

    // ------------------------------------------------------------------ conexión

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
                        link.close()
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
        val adapterView = android.widget.ArrayAdapter(this, android.R.layout.simple_list_item_1, names)
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
