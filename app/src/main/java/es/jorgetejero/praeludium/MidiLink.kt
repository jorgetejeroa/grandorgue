package es.jorgetejero.praeludium

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper

/** Conexión con el órgano: por cable MIDI-USB o por Bluetooth (WIDI). */
class MidiLink(
    ctx: Context,
    private val onInput: (ByteArray, Int, Int, Long) -> Unit,
    private val onStatus: (String) -> Unit,
    private val onDeviceAdded: (MidiDeviceInfo) -> Unit
) {
    val manager = ctx.getSystemService(Context.MIDI_SERVICE) as MidiManager
    private val handler = Handler(Looper.getMainLooper())
    private val sendLock = Any()

    private var device: MidiDevice? = null
    @Volatile private var inPort: MidiInputPort? = null
    private var outPort: MidiOutputPort? = null
    var connectedName: String? = null
        private set

    val isConnected: Boolean get() = inPort != null

    private val receiver = object : MidiReceiver() {
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            onInput(msg, offset, count, timestamp)
        }
    }

    init {
        @Suppress("DEPRECATION")
        manager.registerDeviceCallback(object : MidiManager.DeviceCallback() {
            override fun onDeviceAdded(info: MidiDeviceInfo) {
                this@MidiLink.onDeviceAdded(info)
            }

            override fun onDeviceRemoved(info: MidiDeviceInfo) {
                if (device?.info?.id == info.id) {
                    close()
                    onStatus("Desconectado del órgano")
                }
            }
        }, handler)
    }

    @Suppress("DEPRECATION")
    fun available(): List<MidiDeviceInfo> =
        manager.devices.filter { it.inputPortCount > 0 || it.outputPortCount > 0 }

    fun nameOf(info: MidiDeviceInfo): String {
        val p = info.properties
        return p.getString(MidiDeviceInfo.PROPERTY_NAME)
            ?: p.getString(MidiDeviceInfo.PROPERTY_PRODUCT)
            ?: "Dispositivo MIDI"
    }

    fun open(info: MidiDeviceInfo) {
        close()
        val name = nameOf(info)
        onStatus("Conectando con $name…")
        manager.openDevice(info, { d -> attach(d, name) }, handler)
    }

    @SuppressLint("MissingPermission")
    fun openBle(bt: BluetoothDevice, label: String) {
        close()
        onStatus("Conectando con $label…")
        try {
            manager.openBluetoothDevice(bt, { d -> attach(d, label) }, handler)
        } catch (e: Exception) {
            onStatus("No se pudo conectar con $label")
        }
    }

    private fun attach(d: MidiDevice?, name: String) {
        if (d == null) {
            onStatus("No se pudo conectar con $name")
            return
        }
        device = d
        val info = d.info
        try {
            if (info.outputPortCount > 0) {
                outPort = d.openOutputPort(0)
                outPort?.connect(receiver)
            }
            if (info.inputPortCount > 0) {
                inPort = d.openInputPort(0)
            }
        } catch (e: Exception) { }
        if (inPort == null) {
            onStatus("$name está ocupado por otra app (cierra Midi Voyager u otras)")
            return
        }
        connectedName = name
        onStatus("Conectado: $name")
    }

    fun send(b: ByteArray) {
        val port = inPort ?: return
        synchronized(sendLock) {
            try {
                port.send(b, 0, b.size)
            } catch (e: Exception) { }
        }
    }

    fun close() {
        synchronized(sendLock) {
            try { outPort?.disconnect(receiver) } catch (e: Exception) { }
            try { outPort?.close() } catch (e: Exception) { }
            try { inPort?.close() } catch (e: Exception) { }
            try { device?.close() } catch (e: Exception) { }
            outPort = null
            inPort = null
            device = null
            connectedName = null
        }
    }
}
