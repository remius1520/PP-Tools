package com.premiumpath

// Resource Manager: بيتحكم في تحسين أداء اللعبة وإدارة الموارد في الخلفية.
object ResourceManager {
    
    // تفعيل تحسين الأداء
    fun activateBoost(): String {
        val r = Core.run("resource_boost.sh")
        return if (r.contains("ERROR") || r.isEmpty()) {
            "فشل تفعيل تحسين الأداء. تأكد من وجود الروت."
        } else {
            "تم تفعيل تحسين الأداء بنجاح.\n$r"
        }
    }

    // إلغاء تحسين الأداء: يرجّع oom_score_adj للقيمة اللي كانت قبل التفعيل (المحفوظة في oom_orig.txt)
    fun deactivateBoost(): String {
        val r = Core.suScript("""
            ORIG=/data/local/tmp/pp/oom_orig.txt
            PID=${'$'}(pidof com.PoolLiveTour | cut -d' ' -f1)
            if [ -z "${'$'}PID" ]; then echo "INFO: اللعبة مش شغالة، مفيش حاجة تترجّع (الإعداد بيتصفّر مع تشغيل اللعبة)."; rm -f "${'$'}ORIG"
            elif [ ! -s "${'$'}ORIG" ]; then echo "INFO: التحسين ما اتفعّلش في الجلسة دي."
            else
                OP=${'$'}(cut -d' ' -f1 "${'$'}ORIG"); OV=${'$'}(cut -d' ' -f2 "${'$'}ORIG")
                if [ "${'$'}OP" = "${'$'}PID" ]; then
                    echo "${'$'}OV" > /proc/${'$'}PID/oom_score_adj 2>/dev/null
                    echo "OK: oom_score_adj رجع ${'$'}(cat /proc/${'$'}PID/oom_score_adj)"
                else echo "INFO: اللعبة اتفتحت من جديد، القيمة رجعت طبيعية لوحدها."; fi
                rm -f "${'$'}ORIG"
            fi
        """.trimIndent())
        return if (r.startsWith("OK") || r.startsWith("INFO")) "تم إلغاء تحسين الأداء.\n$r" else "فشل الإلغاء.\n$r"
    }
}
