# FINDINGS P2I (2026-10-31) — بند 2 (fixedDeltaTime) محسوم، بند 1 (enum الدمج) لسه مفتوح

## محسوم (من ملفات اللعبة)
- TimeManager (globalgamemanagers، class 5، 28 بايت): Fixed Timestep = RationalTime { m_Count=1411199 (int64), m_Rate{m_Denominator=1, m_Numerator=141120000} }.
  ترتيب النقل من libunity 0x6a12f4 (m_Denominator الأول عند +0xC ثم m_Numerator عند +0x8).
- تحويل لثواني: libunity 0x6a118c = count × (field+0xC / field+8) (فك يدوي لتعليمات ucvtf/fdiv/fmul) ⇒ **fixedDeltaTime = 1411199/141120000 = 0.00999999291 s** (مش 0.01 بالظبط؛ فرق نسبي 7.1e-7).
  تفسير الفرق (استنتاج): 0.01 كـfloat × 141120000 = 1411199.9968 اتقصّت لـ1411199.
- باقي TimeManager: m_TimeScale=1.36، Maximum Allowed Timestep=1/3، Maximum Particle Timestep=0.09.
- المحرك: `Cfg.dt` بقى `fixedDelta/7` بالقيمة دي. build كامل بيعدّي.

## افتراض (مش محسوم)
- **7 sub-steps**: مصدره الأداة القديمة مش ملفات اللعبة. يحسمه: libil2cpp (Time.fixedDeltaTime/Physics.Simulate في السكربتات) أو libunity.
- هل سكربت اللعبة بيغيّر fixedDeltaTime/timeScale وقت التشغيل؟ (timeScale=1.36 في الملف لا يغيّر خطوة المحاكاة في Unity العادي؛ ده من معرفة عامة). يحسمه: libil2cpp + dump.cs.
- ترتيب/صيغة الـdamping: لسه افتراض.

## بند 1: enum دمج الارتداد — مفتوح
- تخطيط PhysicMaterial الأصلي (من Transfer 0x794f6c): dynFric +0x34، statFric +0x38، bounciness +0x3c، frictionCombine +0x40، bounceCombine +0x44. القيمة في الملف = 2 لكل الـmaterials.
- دوال الـapply (0x78e52c → 0x78e2bc → 0x781e8c) بتبعت {friction,friction,bounce} و{combine pair 64-bit} عبر مؤشر دالة عام في .bss عند 0x16f47d8 (command ids 0x400000D/0x400000F)؛ المؤشر بيتكتب وقت التشغيل ومالقيتش كاتبه بالتحليل الساكن (adrp+str/add+str/GOT)، فالمستلم (اللي بيحوّل للـPhysX) ما اتلقاش.
- بحث بالتوقيع عن سويتش الدمج (fmul/fadd/fmin/fmax + jump table) رجّع مرشّح واحد (0x1338728) واتضح إنه مش ده (كود joint).
- من معرفة عامة (مش من الباينري، غير مؤكد): enum Unity المُدار Average0/Multiply1/Minimum2/Maximum3 ⇒ 2 = Minimum ⇒ 0.92 (زي CHANGES_v3). جملة GAME_FILE_FACTS "Multiply=2" غالباً غلط لكن لسه مش مثبّتة. باند 0.92 مقابل 0.8464 يفضل **افتراض** لحد الحسم.
- مداخل مقترحة للحسم: (أ) إيجاد كاتب 0x16f47d8 (غالباً دالة تسجيل في موديول الفيزياء، ممكن تكون في .init_array أو عبر struct)؛ (ب) إيجاد PxMaterial::setRestitutionCombineMode عبر vtable calls بعد createMaterial.

## أدوات (tools/libunity/)
- `udis.py <start> <end>`: فاك ARM64 لـlibunity.so (مبني على ref/p2e_tools/a64dis.py، بيعرض نصوص adrp+add). المسار الثابت جوه الملف: /home/claude/ext/lib/arm64-v8a/libunity.so.
- `xref.py <hexaddr...>`: مراجع adrp+add لعناوين نصوص في .text.

## بند 3: ما اتبدأش.
