package es.jorgetejero.praeludium

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.text.Collator
import java.util.Locale

data class Entry(val docId: String, val name: String, val isDir: Boolean, val isUp: Boolean = false)

/** Acceso a la carpeta de la tablet elegida por el usuario (y sus subcarpetas). */
class FolderStore(private val ctx: Context, val tree: Uri) {

    val rootId: String = DocumentsContract.getTreeDocumentId(tree)

    fun uriOf(docId: String): Uri = DocumentsContract.buildDocumentUriUsingTree(tree, docId)

    fun nameOf(docId: String): String {
        try {
            ctx.contentResolver.query(uriOf(docId), arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) return c.getString(0) ?: ""
            }
        } catch (e: Exception) { }
        return ""
    }

    fun list(dirId: String): List<Entry> {
        val result = ArrayList<Entry>()
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dirId)
        ctx.contentResolver.query(
            uri,
            arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE),
            null, null, null
        )?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0) ?: continue
                val name = c.getString(1) ?: continue
                val mime = c.getString(2) ?: ""
                if (name.startsWith(".")) continue
                val isDir = mime == Document.MIME_TYPE_DIR
                val lower = name.lowercase(Locale.ROOT)
                if (!isDir && !(lower.endsWith(".mid") || lower.endsWith(".midi"))) continue
                result.add(Entry(id, name, isDir))
            }
        }
        val collator = Collator.getInstance(Locale("es", "ES"))
        return result.sortedWith(Comparator { a, b ->
            if (a.isDir != b.isDir) (if (a.isDir) -1 else 1) else collator.compare(a.name, b.name)
        })
    }

    fun read(docId: String): ByteArray {
        val input = ctx.contentResolver.openInputStream(uriOf(docId)) ?: throw IllegalStateException("No se puede abrir")
        return input.use { it.readBytes() }
    }

    /** Crea un archivo nuevo en la carpeta. Devuelve false si no se pudo. */
    fun create(dirId: String, name: String, bytes: ByteArray): Boolean {
        val dirUri = uriOf(dirId)
        val uri = DocumentsContract.createDocument(ctx.contentResolver, dirUri, "audio/midi", name) ?: return false
        val out = ctx.contentResolver.openOutputStream(uri) ?: return false
        out.use { it.write(bytes) }
        return true
    }
}
