# FINDINGS P2J (2026-10-31) — بند 1 (enum دمج الارتداد): محاولة من dump.cs، النتيجة: لسه مفتوح

## اتجرّب
- `dump.cs` (Unity 6): كلاس `UnityEngine.PhysicsMaterial` (TypeDefIndex 18805) موجود **فاضي** `{}` (من غير حقول/ميثودز)، ومفيش enum اسمه `PhysicMaterialCombine` / `PhysicsMaterialCombine` / `*Combine*` للفيزياء في الدامب (الموجود بس ShadingRateCombiner وحاجات مش متعلقة). يعني il2cpp شال الـenum المُدار؛ مش هينفع يتحسم من dump.cs ولا global-metadata.
- النتيجة: الحسم الوحيد المتبقي من الباينري = libunity (كاتب المؤشر .bss 0x16f47d8 أو استدعاء PxMaterial::setRestitutionCombineMode)، زي ما FINDINGS_P2I حدد. ما اتعملش هنا.

## حساسية (قياس على المحرك، مش مقارنة باللعبة)
مشهد: بيضا من (-0.5, 0) بسرعة (2.5, 1.2)، من غير تصادم كور:
| edge-e | wallHits | موضع الوقوف النهائي (x, z) |
|---|---|---|
| 0.92 (Minimum) | 4 | (-0.9969, -0.3228) |
| 0.8464 (Multiply) | 3 | (-0.9899, -0.4527) |
فرق ~13 سم في ضربة قوية متعددة الحيطان. الفرق في ضربة بانك واحدة أصغر بكتير لكنه موجود.

## افتراض (يفضل)
- 0.92 هو الافتراض الحالي (CLI: `--edge-e 0.8464` لتجربة Multiply). مش \"موثّق المصدر\".
- ملحوظة: قيمة الملف = 2 لكل الـmaterials؛ معنى 2 هو اللي مش محسوم (Minimum أو Multiply حسب ترتيب الـenum).

## الخطوة التالية المقترحة
أصغر طريق للحسم: في libunity دوّر على تحويل القيمة 0..3 لـPxCombineMode (جدول/سويتش صغير قبل createMaterial) بدل ما تدوّر على الكاتب. محتاج `libunity.so` (موجود في ReMius_Unity_Extract.zip) وأدوات tools/libunity.

## محاولة libunity (نفس الجلسة) — ما اتحسمش
- ركّبت libunity.so في /home/claude/ext/lib/arm64-v8a/ وشغّلت tools/libunity/udis.py.
- بحث عن جداول تحويل 0..3 (int32/byte) في الـrodata: {0,2,1,3} int32 = 0 نتيجة؛ byte {0,2,1,3} = 3 نتائج عشوائية (0x1c07cb, 0x1c0bb3, 0x210313) ما اتفحصتش وغالباً مش دي؛ {0,1,2,3} نتايج كتير (ضوضاء).
- بحث عن دوال بتقرا +0x40 و+0x44 جنب بعض (frictionCombine/bounceCombine): 33 مرشّح؛ 0x772518 اتفحص واتضح ring-buffer مش فيزياء. الباقي ما اتفحصش (أولى للفحص: 0x6c853c, 0x9fc6fc/0x9fc864, 0xbc9fe8).
- مستلم command ids 0x400000D/0x400000F لسه مجهول.

## بديل لو الباينري طوّل
قياس واحد فقط بيحسم بين 0.92 و0.8464 (قرار ثنائي مش fit): سرعة بيضا قبل/بعد ارتداد واحد في حيطة من غير سبين. لو عايز تلتزم بقاعدة \"من الملفات بس\" سيبه افتراض.

## تكملة (الجولة التانية)
- فُحصت باقي مرشحي libunity: 0x6c853c = نسخ struct (copy) لحقول material؛ 0x9fc6fc/0x9fc864 = عدّادات (inc/dec) مش دمج؛ 0xbc9fe8 = إعدادات joint. **مفيش مرشح هو سويتش الدمج.** المرشحين الـ33 بحث الحقلين +0x40/+0x44 ما لقاش المحوّل؛ المحوّل غالباً بيتعمل في كود تاني (ممكن يكون inlined عند createMaterial أو بالقيم الخام 0..3 من غير تحويل).
- بند 3 (ربط pp2_spin بالمحاكي): evaluateSpinPipeline محتاج مدخلات runtime لسه مش متحسمة (externalBaseline، cueSpinFactorDividor، leftIncrement/rightIncrement/bottomIncrement، transform vector) = مصدرها قيم الستيك في cuegroup_*.bundle (بند 6). فاتحط افتراض إن Shot.spin/spinAngleDeg بتتدخل مباشرة (زي دلوقتي) لحد ما بند 6 يتحسم. ما اتغيّرش كود.
