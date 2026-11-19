package es.jorgetejero.sl300

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.text.Collator
import java.util.Locale

/** Una fila de la lista de piezas. */
data class Entry(
    val docId: String,
    val name: String,
    val isDir: Boolean,
    val isUp: Boolean = false,
    val moment: String? = null,
    val missing: Boolean = false
)

/** Un archivo o carpeta dentro de la carpeta elegida. */
data class Doc(val docId: String, val name: String, val isDir: Boolean, val modified: Long)

/** Acceso a la carpeta de la tablet elegida por el usuario (y sus subcarpetas). */
class FolderStore(private val ctx: Context, val tree: Uri) {

    companion object {
        const val LISTS_DIR = "Listas"
        const val ORIGINALS_DIR = "Originales"

        fun isMidi(name: String): Boolean {
            val lower = name.lowercase(Locale.ROOT)
            return lower.endsWith(".mid") || lower.endsWith(".midi")
        }
    }

    val rootId: String = DocumentsContract.getTreeDocumentId(tree)
    private val collator: Collator = Collator.getInstance(Locale("es", "ES"))

    fun uriOf(docId: String): Uri = DocumentsContract.buildDocumentUriUsingTree(tree, docId)

    fun nameOf(docId: String): String {
        try {
            ctx.contentResolver.query(uriOf(docId), arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) return c.getString(0) ?: ""
            }
        } catch (e: Exception) { }
        return ""
    }

    fun listAll(dirId: String): List<Doc> {
        val result = ArrayList<Doc>()
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dirId)
        ctx.contentResolver.query(
            uri,
            arrayOf(
                Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                Document.COLUMN_MIME_TYPE, Document.COLUMN_LAST_MODIFIED
            ),
            null, null, null
        )?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0) ?: continue
                val name = c.getString(1) ?: continue
                val mime = c.getString(2) ?: ""
                val modified = if (c.isNull(3)) 0L else c.getLong(3)
                result.add(Doc(id, name, mime == Document.MIME_TYPE_DIR, modified))
            }
        }
        return result
    }

    fun sortByName(list: List<Doc>): List<Doc> = list.sortedWith(Comparator { a, b ->
        if (a.isDir != b.isDir) (if (a.isDir) -1 else 1) else collator.compare(a.name, b.name)
    })

    fun find(dirId: String, name: String): Doc? =
        listAll(dirId).firstOrNull { it.name.equals(name, ignoreCase = true) }

    fun read(docId: String): ByteArray {
        val input = ctx.contentResolver.openInputStream(uriOf(docId)) ?: throw IllegalStateException("No se puede abrir")
        return input.use { it.readBytes() }
    }

    fun readText(docId: String): String = String(read(docId), Charsets.UTF_8).removePrefix("﻿")

    /** Crea un archivo nuevo. Devuelve false si no se pudo. */
    fun create(dirId: String, name: String, bytes: ByteArray, mime: String = "audio/midi"): Boolean {
        val uri = DocumentsContract.createDocument(ctx.contentResolver, uriOf(dirId), mime, name) ?: return false
        val out = ctx.contentResolver.openOutputStream(uri) ?: return false
        out.use { it.write(bytes) }
        return true
    }

    /** Escribe un archivo, sustituyéndolo si ya existe. */
    fun writeFile(dirId: String, name: String, bytes: ByteArray, mime: String = "application/octet-stream"): Boolean {
        val existing = find(dirId, name)
        if (existing == null || existing.isDir) return create(dirId, name, bytes, mime)
        val out = ctx.contentResolver.openOutputStream(uriOf(existing.docId), "wt") ?: return false
        out.use { it.write(bytes) }
        return true
    }

    fun delete(dirId: String, name: String): Boolean {
        val existing = find(dirId, name) ?: return true
        return try {
            DocumentsContract.deleteDocument(ctx.contentResolver, uriOf(existing.docId))
        } catch (e: Exception) {
            false
        }
    }

    /** Devuelve la subcarpeta con ese nombre, creándola si no existe. */
    fun ensureDir(parentId: String, name: String): String? {
        listAll(parentId).firstOrNull { it.isDir && it.name.equals(name, ignoreCase = true) }?.let { return it.docId }
        val uri = DocumentsContract.createDocument(ctx.contentResolver, uriOf(parentId), Document.MIME_TYPE_DIR, name)
            ?: return null
        return DocumentsContract.getDocumentId(uri)
    }
}
