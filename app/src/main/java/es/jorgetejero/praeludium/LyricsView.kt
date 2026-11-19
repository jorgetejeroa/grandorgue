package es.jorgetejero.praeludium

import android.app.Presentation
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.view.Display
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.TextView

/** Pantalla negra con la letra en grande, que se ajusta sola al tamaño. */
class LyricsView(ctx: Context) : FrameLayout(ctx) {
    private val tv = TextView(ctx)

    init {
        setBackgroundColor(Color.BLACK)
        tv.setTextColor(Color.WHITE)
        tv.gravity = Gravity.CENTER
        tv.typeface = Typeface.DEFAULT_BOLD
        val pad = (24 * resources.displayMetrics.density).toInt()
        tv.setPadding(pad, pad, pad, pad)
        tv.setAutoSizeTextTypeUniformWithConfiguration(14, 160, 2, TypedValue.COMPLEX_UNIT_SP)
        addView(tv, LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
    }

    fun show(text: String) {
        if (tv.text.toString() != text) tv.text = text
    }
}

/** La letra en la tele, como segunda pantalla (Smart View o cable HDMI). */
class TvPresentation(outer: Context, display: Display) : Presentation(outer, display) {
    var view: LyricsView? = null
        private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val v = LyricsView(context)
        view = v
        setContentView(v)
    }
}
