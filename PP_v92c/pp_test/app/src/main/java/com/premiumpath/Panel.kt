package com.premiumpath

import android.content.Context
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.widget.LinearLayout
import android.widget.TextView

// The settings panel (shown inside the floating window). Every setting is written to the app prefs and pushed to the game's pp_cfg.txt at once (no restart).
// Shell work runs off the main thread; its result lands in the status line at the bottom of the panel.
object Panel {
    private val ui = Handler(Looper.getMainLooper())

    private fun seg(c: Context, title: String, labels: List<String>, cur: Int, pick: (Int) -> Unit): LinearLayout {
        val box = Ui.section(c, title); val (v, select) = Ui.segmented(c, labels) { pick(it); Thread { Core.pushCfg() }.start() }; select(cur); box.addView(v); return box
    }

    fun build(c: Context, onClose: () -> Unit): LinearLayout {
        val status = TextView(c).apply { setTextColor(Color.parseColor(Ui.GREEN)); textSize = 11f; text = "جاهز"; setPadding(0, Ui.dp(c, 8), 0, 0) }
        fun brief(r: String): String { val ls = r.lines(); if (ls.size <= 7) return r
            val key = ls.filter { it.startsWith("MISSING") || it.startsWith("RESULT") || it.startsWith("saved") || it.startsWith("OK:") || it.startsWith("X") || it.startsWith("!") }
            return (if (key.isNotEmpty()) key.take(8) else ls.takeLast(7)).joinToString("\n") }
        fun async(label: String, work: () -> String) {
            status.text = "$label ..."
            Thread { val r = try { work() } catch (e: Exception) { "فشل: " + e.message }; ui.post { status.text = label + "\n" + brief(r) } }.start()
        }
        fun push() { Thread { Core.pushCfg() }.start() }
        val root = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; setPadding(Ui.dp(c, 10), Ui.dp(c, 10), Ui.dp(c, 10), Ui.dp(c, 10)); background = Ui.shape(c, Ui.BG, Ui.EDGE, 16) }

        val settings = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL }
        val tools = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; visibility = android.view.View.GONE }
        root.addView(TextView(c).apply { text = "PREMIUM PATH  ·  الإعدادات"; setTextColor(Color.parseColor(Ui.GREEN)); textSize = 13f; letterSpacing = 0.15f; setPadding(0, 0, 0, Ui.dp(c, 8)) })
        settings.addView(seg(c, "الخط الدقيق (فيزياء اللعبة نفسها)", listOf("مقفول", "دقيق + تقدير", "دقيق بس"), Core.live) { Core.live = it })
        settings.addView(seg(c, "التقدير السريع (خط خافت)", listOf("مقفول", "شغال"), Core.draft) { Core.draft = it })
        settings.addView(seg(c, "اقتراح الضربة (9 بول + الكسر)", listOf("مقفول", "شغال"), Core.suggest) { Core.suggest = it })
        settings.addView(seg(c, "كتم أحداث التصادم أثناء الحساب", listOf("مقفول", "شغال"), Core.gate) { Core.gate = it })
        settings.addView(seg(c, "اتجاه الخط الدقيق", listOf("قوة الضربة", "اتجاه التصويب"), Core.livedir) { Core.livedir = it })
        settings.addView(seg(c, "ألوان الخطوط", listOf("لون الكورة", "حسب الدور"), Core.colormode) { Core.colormode = it })
        settings.addView(seg(c, "لوج تفصيلي", listOf("مقفول", "شغال"), Core.diag) { Core.diag = it })
        settings.addView(Ui.stepper(c, "زمن الخط الدقيق (ثانية)", "كام ثانية من حركة الكور تتحسب", 1, 10, { Core.livet }, { Core.livet = it; push() }))
        settings.addView(Ui.stepper(c, "ميزانية الحساب (ms)", "لو اللعبة بتهنّج قلّلها", 20, 200, { Core.livems }, { Core.livems = it; push() }))
        settings.addView(Ui.stepper(c, "ارتدادات التقدير", "للخط الخافت فقط", 0, 8, { Core.bounces }, { Core.bounces = it; push() }))
        settings.addView(Ui.stepper(c, "أقصى عدد خطوط", "", 20, 120, { Core.maxlines }, { Core.maxlines = it; push() }))
        fun half(t: String, color: String = Ui.CYAN, click: () -> Unit): android.widget.Button = Ui.action(c, t, color, click).apply { textSize = 12f; layoutParams = LinearLayout.LayoutParams(0, Ui.dp(c, 40), 1f).apply { setMargins(Ui.dp(c, 2), 0, Ui.dp(c, 2), Ui.dp(c, 6)) } }
        fun row2(x: android.widget.Button, y: android.widget.Button): LinearLayout = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(-1, -2); addView(x); addView(y) }
        tools.addView(row2(half("حفظ اللوج") { async("اللوج") { Core.saveLog() } }, half("تشخيص") { async("تشخيص") { Core.diagnose() } }))
        tools.addView(row2(half("فحص PP2") { async("فحص PP2") { Core.probePP2() } }, half("تجهيز تسجيل ضربة") { async("تسجيل ضربة") { Core.armShot() } }))
        tools.addView(row2(half("حفظ الضربة") { async("حفظ الضربة") { Core.saveShot() } }, half("إغلاق الزر العايم", Ui.RED) { onClose() }))
        // two pages so every button is reachable without scrolling to the bottom: settings (sliders) and tools (log / diagnose / PP2 probe / shot recorder / close)
        fun showPage(t: Boolean) { tools.visibility = if (t) android.view.View.VISIBLE else android.view.View.GONE; settings.visibility = if (t) android.view.View.GONE else android.view.View.VISIBLE }
        val tabs = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(-1, -2) }
        fun tab(t: String, toTools: Boolean): android.widget.Button = Ui.action(c, t) { showPage(toTools) }.apply { layoutParams = LinearLayout.LayoutParams(0, Ui.dp(c, 40), 1f).apply { setMargins(Ui.dp(c, 2), 0, Ui.dp(c, 2), Ui.dp(c, 6)) } }
        tabs.addView(tab("الإعدادات", false)); tabs.addView(tab("الأدوات", true))
        root.addView(tabs)
        root.addView(settings)
        root.addView(tools)
        root.addView(status)
        return root
    }
}
