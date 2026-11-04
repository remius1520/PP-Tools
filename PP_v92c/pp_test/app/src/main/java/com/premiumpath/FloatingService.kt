package com.premiumpath

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.PixelFormat
import android.os.IBinder
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.LinearLayout
import android.widget.ScrollView

// The floating button: a draggable face that opens the settings panel on tap. One overlay window (face on top, panel under it, hidden until tapped).
// Needs the "display over other apps" permission (the main screen grants it through root when it can, otherwise opens the system page).
class FloatingService : Service() {
    private lateinit var wm: WindowManager
    private var box: LinearLayout? = null
    private var panel: View? = null
    private lateinit var lp: WindowManager.LayoutParams
    private var open = false

    override fun onBind(i: Intent?): IBinder? = null

    override fun onStartCommand(i: Intent?, flags: Int, id: Int): Int {
        Core.init(this)
        startForeground(1, notification())
        if (box == null) show()
        return START_STICKY
    }

    private fun notification(): Notification {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.createNotificationChannel(NotificationChannel("pp_float", "PremiumPath", NotificationManager.IMPORTANCE_MIN))
        return Notification.Builder(this, "pp_float").setSmallIcon(android.R.drawable.ic_menu_manage).setContentTitle("PremiumPath").setContentText("الزر العايم شغال").build()
    }

    private fun show() {
        wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val c = this
        val sp = getSharedPreferences("ppbubble", Context.MODE_PRIVATE)
        lp = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL, PixelFormat.TRANSLUCENT
        ).apply { gravity = Gravity.TOP or Gravity.START; x = sp.getInt("x", 0); y = sp.getInt("y", Ui.dp(c, 120)) }

        val face = Ui.face(c, 52)
        val dm = resources.displayMetrics
        val pw = minOf(dm.widthPixels - Ui.dp(c, 16), Ui.dp(c, 340)); val ph = minOf((dm.heightPixels * 0.9).toInt(), dm.heightPixels - Ui.dp(c, 96))   // v88: was 0.7 of the screen: in landscape the lower buttons were cut off
        val p = ScrollView(c).apply { visibility = View.GONE; layoutParams = LinearLayout.LayoutParams(pw, ph) }
        p.addView(Panel.build(c) { stopSelf() })
        panel = p

        val b = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; addView(face); addView(p) }
        box = b

        var dx = 0f; var dy = 0f; var sx = 0; var sy = 0; var moved = false
        face.setOnTouchListener { _, e ->
            when (e.action) {
                MotionEvent.ACTION_DOWN -> { dx = e.rawX; dy = e.rawY; sx = lp.x; sy = lp.y; moved = false; true }
                MotionEvent.ACTION_MOVE -> {
                    val mx = e.rawX - dx; val my = e.rawY - dy
                    if (moved || Math.abs(mx) + Math.abs(my) > Ui.dp(c, 8)) { moved = true; lp.x = (sx + mx).toInt().coerceIn(0, dm.widthPixels - Ui.dp(c, 52)); lp.y = (sy + my).toInt().coerceIn(0, dm.heightPixels - Ui.dp(c, 52)); wm.updateViewLayout(b, lp) }
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (moved) sp.edit().putInt("x", lp.x).putInt("y", lp.y).apply() else toggle(pw, ph)
                    true
                }
                else -> false
            }
        }
        try { wm.addView(b, lp) } catch (e: Exception) { box = null; stopSelf() }   // no overlay permission: nothing to show
    }

    private fun toggle(pw: Int, ph: Int) {
        open = !open; panel?.visibility = if (open) View.VISIBLE else View.GONE
        if (open) { val dm = resources.displayMetrics; lp.x = lp.x.coerceIn(0, maxOf(0, dm.widthPixels - pw)); lp.y = lp.y.coerceIn(0, maxOf(0, dm.heightPixels - ph - Ui.dp(this, 60))) }
        box?.let { wm.updateViewLayout(it, lp) }
    }

    override fun onDestroy() { try { box?.let { wm.removeView(it) } } catch (_: Exception) {}; box = null; super.onDestroy() }
}
