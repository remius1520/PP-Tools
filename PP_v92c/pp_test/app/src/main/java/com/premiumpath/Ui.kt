package com.premiumpath

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Outline
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewOutlineProvider
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

// Shared look: luxury black with a glowing blood-red accent and gold headings. Used by the main screen and the floating panel.
object Ui {
    const val BG = "#07060A"; const val CARD = "#0F0A0E"; const val EDGE = "#4A0F1A"; const val INK = "#160B11"
    const val CYAN = "#FF2A44"      // the accent (glowing blood red). The old name is kept so every call site still compiles.
    const val GREEN = "#E3BE78"     // gold: titles and status text
    const val RED = "#C2283C"       // destructive actions
    const val TEXT = "#F7E9EB"; const val DIM = "#A78B90"

    fun dp(c: Context, v: Int) = (v * c.resources.displayMetrics.density).toInt()

    fun shape(c: Context, fill: String, stroke: String?, radius: Int): GradientDrawable = GradientDrawable().apply {
        setColor(Color.parseColor(fill)); cornerRadius = dp(c, radius).toFloat(); if (stroke != null) setStroke(dp(c, 1), Color.parseColor(stroke))
    }

    // a soft red halo: a faint wide outline around the normal shape (fake glow, no blur needed)
    fun glow(c: Context, fill: String, color: String, radius: Int): LayerDrawable {
        val halo = GradientDrawable().apply {
            setColor(Color.TRANSPARENT); cornerRadius = dp(c, radius + 3).toFloat(); setStroke(dp(c, 3), (Color.parseColor(color) and 0x00FFFFFF) or 0x40000000)
        }
        return LayerDrawable(arrayOf(halo, shape(c, fill, color, radius))).apply { setLayerInset(1, dp(c, 3), dp(c, 3), dp(c, 3), dp(c, 3)) }
    }

    fun card(c: Context): LinearLayout = LinearLayout(c).apply {
        orientation = LinearLayout.VERTICAL; background = shape(c, CARD, EDGE, 14); setPadding(dp(c, 12), dp(c, 10), dp(c, 12), dp(c, 12))
        layoutParams = LinearLayout.LayoutParams(-1, -2).apply { setMargins(0, 0, 0, dp(c, 8)) }
    }

    // a titled box: gold title, a thin red rule, then the controls of that box
    fun section(c: Context, title: String): LinearLayout = card(c).apply {
        addView(TextView(c).apply { text = title; textSize = 13f; letterSpacing = 0.12f; setTextColor(Color.parseColor(GREEN)); setPadding(0, 0, 0, dp(c, 4)) })
        addView(View(c).apply { setBackgroundColor(Color.parseColor(CYAN)); layoutParams = LinearLayout.LayoutParams(-1, dp(c, 1)).apply { setMargins(0, 0, 0, dp(c, 8)) } })
    }

    fun label(c: Context, t: String, size: Float = 12f, color: String = DIM): TextView = TextView(c).apply { text = t; textSize = size; setTextColor(Color.parseColor(color)); setPadding(0, 0, 0, dp(c, 6)) }

    fun chip(c: Context, t: String, selected: Boolean = false, click: () -> Unit): Button = Button(c).apply {
        text = t; textSize = 12f; isAllCaps = false; minWidth = 0; minHeight = 0; minimumWidth = 0; minimumHeight = 0; stateListAnimator = null
        setPadding(dp(c, 6), dp(c, 6), dp(c, 6), dp(c, 6)); gravity = Gravity.CENTER
        setTextColor(Color.parseColor(if (selected) BG else TEXT)); background = shape(c, if (selected) CYAN else INK, if (selected) CYAN else "#B3394D", 10)
        layoutParams = LinearLayout.LayoutParams(0, dp(c, 36), 1f).apply { setMargins(dp(c, 2), dp(c, 2), dp(c, 2), dp(c, 2)) }
        setOnClickListener { click() }
    }

    fun action(c: Context, t: String, color: String = CYAN, click: () -> Unit): Button = Button(c).apply {
        text = t; textSize = 13f; isAllCaps = false; stateListAnimator = null; setTextColor(Color.parseColor(if (color == CYAN) TEXT else color)); background = glow(c, INK, color, 12)
        layoutParams = LinearLayout.LayoutParams(-1, dp(c, 44)).apply { setMargins(0, 0, 0, dp(c, 6)) }
        setOnClickListener { click() }
    }

    // A row of numbered chips. The selected one is lit; pressing one calls pick(n) and re-lights the row.
    fun chipRow(c: Context, values: List<Int>, current: () -> Int, pick: (Int) -> Unit): LinearLayout {
        val row = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(-1, -2) }
        val btns = ArrayList<Button>()
        fun paint() { for ((i, b) in btns.withIndex()) { val sel = values[i] == current(); b.setTextColor(Color.parseColor(if (sel) BG else TEXT)); b.background = shape(c, if (sel) CYAN else INK, if (sel) CYAN else EDGE, 10) } }
        for (v in values) { val b = chip(c, v.toString()) { pick(v); paint() }; btns.add(b); row.addView(b) }
        paint(); return row
    }


    // a big main-screen button
    fun big(c: Context, t: String, color: String = CYAN, click: () -> Unit): Button = Button(c).apply {
        text = t; textSize = 16f; isAllCaps = false; stateListAnimator = null; setTextColor(Color.parseColor(if (color == CYAN) TEXT else color)); background = glow(c, INK, color, 16)
        layoutParams = LinearLayout.LayoutParams(-1, dp(c, 60)).apply { setMargins(0, 0, 0, dp(c, 12)) }
        setOnClickListener { click() }
    }

    // a small gold sub-heading with a one-line explanation under it (used inside the panel boxes)
    fun hint(c: Context, title: String, what: String): LinearLayout = LinearLayout(c).apply {
        orientation = LinearLayout.VERTICAL; setPadding(0, dp(c, 4), 0, dp(c, 4))
        addView(TextView(c).apply { text = title; textSize = 12f; setTextColor(Color.parseColor(TEXT)) })
        addView(TextView(c).apply { text = what; textSize = 10f; setTextColor(Color.parseColor(DIM)); setPadding(0, 0, 0, dp(c, 3)) })
    }


    // ---------------- v77: one look for every control of the panel ----------------
    // Rules: radius 12 on tiles / 10 on small buttons / 9 on the lit tab; 1dp edge; the accent colour only marks "on" or "selected";
    // gold is for titles and status; red text only for destructive actions. No halo on buttons.
    const val ON_FILL = "#2B0D16"

    // A tool tile. tone: "n" normal | "p" primary (gold edge) | "d" danger (red) | "m" master (filled with the accent when on).
    // setState(on) makes it a toggle (second line shows the state); setPicked(b) only lights it (radio-like groups).
    class Tile(private val c: Context, label: String, sub: String? = null, private val tone: String = "n", click: () -> Unit) {
        val view = LinearLayout(c)
        private val t1 = TextView(c)
        private val t2 = TextView(c)
        private var st = false
        private var picked = false

        init {
            view.orientation = LinearLayout.VERTICAL; view.gravity = Gravity.CENTER; view.isClickable = true
            view.setPadding(dp(c, 6), dp(c, 4), dp(c, 6), dp(c, 4))
            t1.text = label; t1.textSize = 12f; t1.gravity = Gravity.CENTER; t1.maxLines = 2
            t2.text = sub ?: ""; t2.textSize = 10f; t2.gravity = Gravity.CENTER; t2.maxLines = 1
            t2.visibility = if (sub.isNullOrEmpty()) View.GONE else View.VISIBLE
            view.addView(t1); view.addView(t2)
            view.setOnClickListener { click() }
            paint()
        }

        fun setText(l: String, s: String? = null) {
            t1.text = l
            if (s != null) { t2.text = s; t2.visibility = if (s.isEmpty()) View.GONE else View.VISIBLE }
        }

        fun setState(on: Boolean, onT: String = "شغّال", offT: String = "مقفول") {
            st = on; t2.text = if (on) onT else offT; t2.visibility = View.VISIBLE; paint()
        }

        fun setPicked(b: Boolean) { picked = b; paint() }

        private fun paint() {
            val lit = st || picked
            val master = tone == "m"
            val fill = if (master && lit) CYAN else if (lit) ON_FILL else INK
            val stroke = if (lit) CYAN else when (tone) { "p" -> "#8A6F3A"; "d" -> "#7A1F2B"; else -> EDGE }
            val ink = if (master && lit) BG else if (lit) TEXT else when (tone) { "d" -> "#E4707F"; "p" -> GREEN; else -> TEXT }
            val subInk = if (master && lit) BG else if (lit) GREEN else DIM
            t1.setTextColor(Color.parseColor(ink)); t2.setTextColor(Color.parseColor(subInk))
            view.background = RippleDrawable(ColorStateList.valueOf(0x33FF2A44), shape(c, fill, stroke, 12), null)
        }
    }

    // tiles side by side, `cols` per row, equal width and height; the last row is padded with empty cells so nothing stretches
    fun grid(c: Context, cols: Int, tiles: List<Tile>): LinearLayout {
        val col = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(-1, -2) }
        var i = 0
        while (i < tiles.size) {
            val row = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(-1, -2) }
            for (k in 0 until cols) {
                val v: View = tiles.getOrNull(i + k)?.view ?: View(c)
                v.layoutParams = LinearLayout.LayoutParams(0, dp(c, 56), 1f).apply { setMargins(dp(c, 3), dp(c, 3), dp(c, 3), dp(c, 3)) }
                row.addView(v)
            }
            col.addView(row); i += cols
        }
        return col
    }

    // a number with - and + (replaces the old rows of numbered chips)
    fun stepper(c: Context, title: String, what: String, min: Int, max: Int, read: () -> Int, write: (Int) -> Unit): LinearLayout {
        val row = LinearLayout(c).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            background = shape(c, CARD, EDGE, 12); setPadding(dp(c, 10), dp(c, 6), dp(c, 6), dp(c, 6))
            layoutParams = LinearLayout.LayoutParams(-1, -2).apply { setMargins(dp(c, 3), dp(c, 3), dp(c, 3), dp(c, 3)) }
        }
        val txt = LinearLayout(c).apply {
            orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
            addView(TextView(c).apply { text = title; textSize = 12f; setTextColor(Color.parseColor(TEXT)) })
            addView(TextView(c).apply { text = what; textSize = 10f; setTextColor(Color.parseColor(DIM)) })
        }
        val value = TextView(c).apply { textSize = 16f; gravity = Gravity.CENTER; setTextColor(Color.parseColor(GREEN)); minWidth = dp(c, 32) }
        fun refresh() { value.text = read().toString() }
        fun btn(sym: String, d: Int): TextView = TextView(c).apply {
            text = sym; textSize = 18f; gravity = Gravity.CENTER; isClickable = true; setTextColor(Color.parseColor(TEXT))
            background = RippleDrawable(ColorStateList.valueOf(0x33FF2A44), shape(c, INK, EDGE, 10), null)
            layoutParams = LinearLayout.LayoutParams(dp(c, 36), dp(c, 36))
            setOnClickListener { write((read() + d).coerceIn(min, max)); refresh() }
        }
        row.addView(txt); row.addView(btn("\u2212", -1)); row.addView(value); row.addView(btn("+", 1))
        refresh(); return row
    }

    // v81: same as stepper() but with decimal values (the config panel)
    fun fstepper(c: Context, title: String, what: String, min: Double, max: Double, step: Double, read: () -> Double, write: (Double) -> Unit): LinearLayout {
        val row = LinearLayout(c).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            background = shape(c, CARD, EDGE, 12); setPadding(dp(c, 10), dp(c, 6), dp(c, 6), dp(c, 6))
            layoutParams = LinearLayout.LayoutParams(-1, -2).apply { setMargins(dp(c, 3), dp(c, 3), dp(c, 3), dp(c, 3)) }
        }
        val txt = LinearLayout(c).apply {
            orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
            addView(TextView(c).apply { text = title; textSize = 12f; setTextColor(Color.parseColor(TEXT)) })
            addView(TextView(c).apply { text = what; textSize = 10f; setTextColor(Color.parseColor(DIM)) })
        }
        val value = TextView(c).apply { textSize = 15f; gravity = Gravity.CENTER; setTextColor(Color.parseColor(GREEN)); minWidth = dp(c, 52) }
        fun fmt(v: Double): String = if (Math.abs(v - Math.round(v).toDouble()) < 1e-9) Math.round(v).toString() else String.format(java.util.Locale.US, "%.3f", v).trimEnd('0').trimEnd('.')
        fun refresh() { value.text = fmt(read()) }
        fun btn(sym: String, d: Int): TextView = TextView(c).apply {
            text = sym; textSize = 18f; gravity = Gravity.CENTER; isClickable = true; setTextColor(Color.parseColor(TEXT))
            background = RippleDrawable(ColorStateList.valueOf(0x33FF2A44), shape(c, INK, EDGE, 10), null)
            layoutParams = LinearLayout.LayoutParams(dp(c, 36), dp(c, 36))
            setOnClickListener { val nv = (Math.round((read() + d * step) / step) * step * 1e6).let { Math.round(it) / 1e6 }; write(nv.coerceIn(min, max)); refresh() }
        }
        row.addView(txt); row.addView(btn("\u2212", -1)); row.addView(value); row.addView(btn("+", 1))
        refresh(); return row
    }

    // two or more tabs in one rounded bar; the picked one is filled with the accent. Returns the bar and a function that lights a tab.
    fun segmented(c: Context, labels: List<String>, pick: (Int) -> Unit): Pair<LinearLayout, (Int) -> Unit> {
        val wrap = LinearLayout(c).apply {
            orientation = LinearLayout.HORIZONTAL; background = shape(c, INK, EDGE, 12); setPadding(dp(c, 3), dp(c, 3), dp(c, 3), dp(c, 3))
            layoutParams = LinearLayout.LayoutParams(-1, dp(c, 40))
        }
        val items = ArrayList<TextView>()
        fun select(i: Int) { for ((k, t) in items.withIndex()) { val on = k == i; t.setTextColor(Color.parseColor(if (on) BG else DIM)); t.background = if (on) shape(c, CYAN, null, 9) else null } }
        for ((i, l) in labels.withIndex()) {
            items.add(TextView(c).apply { text = l; textSize = 13f; gravity = Gravity.CENTER; isClickable = true; layoutParams = LinearLayout.LayoutParams(0, -1, 1f); setOnClickListener { select(i); pick(i) } })
        }
        for (t in items) wrap.addView(t)
        select(0)
        return Pair(wrap, { i: Int -> select(i) })
    }

    // the round face used as the floating button and in the header
    fun face(c: Context, size: Int): ImageView = ImageView(c).apply {
        setImageResource(R.drawable.bubble_face); scaleType = ImageView.ScaleType.CENTER_CROP
        outlineProvider = object : ViewOutlineProvider() { override fun getOutline(v: View, o: Outline) { o.setOval(0, 0, v.width, v.height) } }
        clipToOutline = true
        foreground = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(Color.TRANSPARENT); setStroke(dp(c, 2), Color.parseColor(CYAN)) }
        elevation = dp(c, 6).toFloat()
        layoutParams = LinearLayout.LayoutParams(dp(c, size), dp(c, size))
    }
}
