package com.premiumpath

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

// Main screen: install + restart the game, remove, performance boost, and the switch for the floating button.
// Every setting lives in the floating panel (Panel.kt / FloatingService.kt). Shell logic is in shell scripts under assets.
class MainActivity : Activity() {
    private lateinit var status: TextView

    private fun async(label: String, work: () -> String) {
        status.text = "$label ..."
        Thread { val r = work(); runOnUiThread { status.text = "$label\n$r" } }.start()
    }

    private fun startBubble(): String {
        if (!Settings.canDrawOverlays(this)) {
            // rooted device: grant "display over other apps" directly; otherwise open the system page
            Core.suScript("appops set $packageName SYSTEM_ALERT_WINDOW allow")
            Thread.sleep(300)
        }
        if (!Settings.canDrawOverlays(this)) {
            runOnUiThread { startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))) }
            return "فعّل إذن \"العرض فوق التطبيقات\" لـ PremiumPath ثم اضغط الزر تاني."
        }
        startForegroundService(Intent(this, FloatingService::class.java))
        return "الزر العايم شغال فوق اللعبة. اضغط عليه للإعدادات، واسحبه لتحريكه."
    }

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        Core.init(this)
        val c = this
        status = TextView(this).apply { setTextColor(Color.parseColor(Ui.GREEN)); textSize = 12f; text = "جاهز. محتاج روت. الإعدادات في الزر العايم."; setPadding(0, Ui.dp(c, 12), 0, 0) }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
            setPadding(Ui.dp(c, 18), Ui.dp(c, 24), Ui.dp(c, 18), Ui.dp(c, 18)); setBackgroundColor(Color.parseColor(Ui.BG))
        }
        root.addView(Ui.face(c, 80))
        root.addView(TextView(this).apply { text = "PREMIUM PATH  ·  LIVE"; setTextColor(Color.parseColor(Ui.GREEN)); textSize = 18f; letterSpacing = 0.2f; gravity = Gravity.CENTER; setPadding(0, Ui.dp(c, 8), 0, Ui.dp(c, 16)) })

        root.addView(Ui.big(c, "تثبيت + إعادة تشغيل اللعبة", Ui.GREEN) { async("تثبيت") { Core.install() + "\n" + startBubble() } })
        root.addView(Ui.big(c, "إزالة (اللعبة ترجع عادية)", Ui.RED) { async("إزالة") { stopService(Intent(c, FloatingService::class.java)); Core.remove() } })
        root.addView(Ui.big(c, "الزر العايم (الإعدادات)", Ui.CYAN) { async("الزر العايم") { startBubble() } })
        root.addView(Ui.big(c, "فحص PP2 (OK / MISSING)", Ui.CYAN) { async("فحص PP2") { Core.probePP2() } })
        root.addView(Ui.big(c, "تجهيز تسجيل ضربة", Ui.CYAN) { async("تسجيل ضربة") { Core.armShot() } })
        root.addView(Ui.big(c, "حفظ الضربة", Ui.CYAN) { async("حفظ الضربة") { Core.saveShot() } })
        root.addView(Ui.big(c, "تحسين اللعبة (Boost)", Ui.GREEN) { async("تحسين اللعبة") { Core.boost() } })
        root.addView(Ui.big(c, "إلغاء تحسين اللعبة", Ui.RED) { async("إلغاء التحسين") { Core.unboost() } })
        root.addView(status)
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor(Ui.BG)); isFillViewport = true; addView(root) })
    }
}
