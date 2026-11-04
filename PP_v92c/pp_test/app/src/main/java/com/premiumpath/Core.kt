package com.premiumpath

import android.content.Context

// Root helper for the main screen. The payload lives inside the game; this app only installs it, writes pp_cfg.txt and reads the log. There is NO overlay window any more.
object Core {
    const val pkg = "com.PoolLiveTour"
    private lateinit var app: Context
    private val prefs get() = app.getSharedPreferences("pp", Context.MODE_PRIVATE)

    // settings (persisted in the app, pushed to the game's pp_cfg.txt)
    var live: Int get() = prefs.getInt("live", 1); set(v) { prefs.edit().putInt("live", v).apply() }          // 0 off, 1 exact path + faint preview, 2 exact only
    var draft: Int get() = prefs.getInt("draft", 1); set(v) { prefs.edit().putInt("draft", v).apply() }
    var suggest: Int get() = prefs.getInt("suggest", 1); set(v) { prefs.edit().putInt("suggest", v).apply() }
    var gate: Int get() = prefs.getInt("gate", 1); set(v) { prefs.edit().putInt("gate", v).apply() }
    var diag: Int get() = prefs.getInt("diag", 1); set(v) { prefs.edit().putInt("diag", v).apply() }
    var livedir: Int get() = prefs.getInt("livedir", 0); set(v) { prefs.edit().putInt("livedir", v).apply() }
    var colormode: Int get() = prefs.getInt("colormode", 0); set(v) { prefs.edit().putInt("colormode", v).apply() }
    var maxlines: Int get() = prefs.getInt("maxlines", 70); set(v) { prefs.edit().putInt("maxlines", v).apply() }
    var livet: Int get() = prefs.getInt("livet", 4); set(v) { prefs.edit().putInt("livet", v).apply() }       // simulated seconds of the exact path
    var livems: Int get() = prefs.getInt("livems", 60); set(v) { prefs.edit().putInt("livems", v).apply() }   // wall-clock budget (ms) of one exact run
    var bounces: Int get() = prefs.getInt("bounces", 3); set(v) { prefs.edit().putInt("bounces", v).apply() }
    var chain: Int get() = prefs.getInt("chain", 4); set(v) { prefs.edit().putInt("chain", v).apply() }
    var rec: Int get() = prefs.getInt("rec", 0); set(v) { prefs.edit().putInt("rec", v).apply() }             // v87: a new number = "arm the one-shot recorder"
    var probe: Int get() = prefs.getInt("probe", 0); set(v) { prefs.edit().putInt("probe", v).apply() }       // v87: a new number = "run the PP2 name probe now"

    fun init(c: Context) { app = c.applicationContext }
    private fun asset(name: String) = app.assets.open(name).bufferedReader().readText()

    // ONE persistent root shell (su -mm = global mount namespace): every script is fed through stdin and read up to an end marker.
    private object RootShell {
        private const val END = "__PP_END_7f3a91__"
        private var proc: Process? = null; private var w: java.io.BufferedWriter? = null; private var r: java.io.BufferedReader? = null
        private fun alive(): Boolean = proc?.let { try { it.exitValue(); false } catch (_: IllegalThreadStateException) { true } } ?: false
        private fun stop() { try { proc?.destroy() } catch (_: Exception) {}; proc = null; w = null; r = null }
        private fun open(args: Array<String>): Boolean {
            stop()
            return try {
                val p = ProcessBuilder(*args).redirectErrorStream(true).start(); proc = p; w = p.outputStream.bufferedWriter(); r = p.inputStream.bufferedReader()
                w!!.write("echo $END\n"); w!!.flush(); val sb = StringBuilder(); var ok = false
                while (true) { val l = r!!.readLine() ?: break; if (l == END) { ok = true; break }; sb.append(l).append('\n') }
                if (ok && !Regex("(?i)(invalid|unknown|unrecognized) option|usage:").containsMatchIn(sb.take(300))) true else { stop(); false }
            } catch (e: Exception) { stop(); false }
        }
        private fun ensure(): Boolean = alive() || open(arrayOf("su", "-mm")) || open(arrayOf("su"))
        @Synchronized fun exec(script: String): String {
            for (attempt in 1..2) {
                if (!ensure()) return "su failed: no root shell"
                try {
                    val tag = "PP_S_" + System.nanoTime(); w!!.write("sh <<'$tag'\n"); w!!.write(script); w!!.write("\n$tag\necho $END\n"); w!!.flush()
                    val sb = StringBuilder(); while (true) { val l = r!!.readLine() ?: throw java.io.IOException("su closed"); if (l == END) break; sb.append(l).append('\n') }
                    return sb.toString().trim()
                } catch (e: Exception) { stop(); if (attempt == 2) return "su failed: ${e.message}" }
            }
            return "su failed"
        }
    }
    fun suScript(script: String): String = RootShell.exec(script)
    fun run(name: String, extra: Map<String, String> = emptyMap()): String {
        var s = (asset("pp_common.sh") + "\n" + asset(name)).replace("@PKG@", pkg)
        for ((k, v) in extra) s = s.replace(k, v)
        return suScript(s)
    }

    fun cfgBody(): String = "on=1\\nlive=$live\\ndraft=$draft\\nsuggest=$suggest\\ngate=$gate\\ndiag=$diag\\nlivedir=$livedir\\ncolormode=$colormode\\nmaxlines=$maxlines\\nlivet=$livet\\nlivems=$livems\\nbounces=$bounces\\nchain=$chain\\nprobe=$probe\\nrec=$rec\\n"
    fun pushCfg(): String = run("write_cfg.sh", mapOf("@BODY@" to cfgBody()))   // the payload re-reads pp_cfg.txt every second, no restart needed

    fun install(): String { val r = run("install.sh", mapOf("@LIB@" to app.applicationInfo.nativeLibraryDir + "/libpremiumpath.so")); Thread.sleep(6000); return r + "\n" + pushCfg() }
    fun remove(): String = run("remove.sh")
    fun diagnose(): String { val r = run("diagnose.sh"); suScript("cat > /sdcard/Download/pp_report.txt <<'PPEOF'\n" + r + "\nPPEOF"); return r + "\n(saved: Download/pp_report.txt)" }
    fun saveLog(): String = run("savelog.sh")
    // v87: PP2 probe. The payload (already inside the game) resolves every class/method/field the PP2 simulator needs and writes OK/MISSING lines to pp2_probe.txt;
    // this copies it to Download/pp2_probe.txt and returns the text.  It also runs once at every game launch, so the file exists even before a match.
    // v87: shot recorder. armShot() arms it (the payload waits for the balls to rest, then records the next shot); saveShot() copies pp2_shot.txt to Download and summarises it.
    fun armShot(): String { val t = (System.currentTimeMillis() / 1000).toInt(); rec = t; pushCfg(); return "جاهز. اضرب دلوقتي ضربة واحدة (الكور واقفة). بعد ما الكور توقف اضغط \"حفظ الضربة\"." }
    fun saveShot(): String = run("shot.sh")
    fun probePP2(): String {
        val t = (System.currentTimeMillis() / 1000).toInt(); probe = t; pushCfg()
        Thread.sleep(4000)   // the payload re-reads pp_cfg.txt every second while the game's CueBallView ticks (in a match)
        return run("probe.sh", mapOf("@T@" to t.toString()))
    }
    fun boost(): String = ResourceManager.activateBoost()
    fun unboost(): String = ResourceManager.deactivateBoost()
}
