# PP v84 LIVE — النسخة المجمّعة (v83 + PP2 P2Z)
**ما اتبناش بـNDK/Gradle ولا اتجرّب على جهاز.** اتجرّب هنا: `sh build.sh` (اختبارات المحرك القديم + 24 اختبار live + 15 paths + 15 unityhost على Unity وهمي + فحص صياغة الـpayload).

## اللي اتشال
- الأوفر لاي كله: `OverlayWindow`, `FloatingService`, إرسال UDP/الملف، إذن SYSTEM_ALERT_WINDOW وINTERNET. الخطوط بتترسم **جوه اللعبة** بـLineRenderer (زي v20+).
- كل الـfit/التعلّم/التسجيل: recorder, capture, query, lobby/trace hooks, verify, dumpfuncs, acc, cuekeep/stopdrag/bbmodel, تعديل قوة الضربة بالـarrow.
- الواجهة بقت شاشة واحدة (تثبيت / إزالة / لوج / إعدادات).

## اللي اتاخد
- من v83: التثبيت (`install.sh`, `pp_common.sh`)، hook الـLateUpdate، حارس الـSIGSEGV، تجمّع خطوط الـLineRenderer، قراءة الكور/الجيوب/الباور، الـPlanner (اقتراح 9 بول + الكسر)، المحرك القديم (`core/`) **كتقدير خافت واحتياطي بس**.
- من PP2: `live::predict` (snapshot → قواعد الضربة P2Q/P2R + ApplySpin P2H → `Physics.Simulate` الحقيقية → restore).

## إزاي بيشتغل
| الحالة | اللي بيترسم |
|---|---|
| ماسك الباور (reelPosition>0.02) والإدخال ثابت 0.10 ث | **خط دقيق صلب** من فيزياء اللعبة نفسها (حساب واحد، ≤ `livems` مللي) |
| بتحرّك/لسه الحساب ما خلصش / مش ماسك الباور | **خط خافت** (تقدير هندسي طويل من v83) |
| الخط الدقيق فشل/مش متاح | الخافت لوحده، واللعبة ما بتتأثرش |
- الحساب بيحصل مرة لما الإدخال يثبت (مش كل فريم) — ده اللي بيمنع اللاج.
- **gate:** callbacks التصادم/الـtrigger بتاعة اللعبة (`OnCollision*/OnTrigger*` في Assembly-CSharp) بتتكتم وقت الحساب بس، فمفيش ApplySpin مزدوج ولا صوت.
- أي كراش داخل الحساب: الكور بترجع مكانها (`emergencyRestore`) والخط الدقيق بيتقفل للجلسة.
- أمان: مفيش `Physics.Simulate` إلا لو `Physics.simulationMode == Script` و`isHit == false` والكور واقفة.

## افتراضات لسه محتاجة الجهاز (A1–A8 في FINDINGS_P2Z.md + دي)
1. أسماء Unity (`Rigidbody.get_position` … `Physics.OverlapSphereNonAlloc`) بتتحل بالاسم؛ اللي مش موجود بيظهر `MISSING` في اللوج (`[H]`)، ولكل واحد fallback (اسم تاني / icall `_Injected`).
2. restore الـTransform مع الـRigidbody (عشان الكور ما تظهرش في مكان نهاية الحساب فريم).
3. الخط الدقيق بيستخدم `forceDirection` للاتجاه والقوة (اللي اللعبة بتضربه فعلاً)؛ لو ظهر فرق عن التصويب جرّب "اتجاه التصويب" في الإعدادات.
4. `ShotData.SpinApplied` لسه مش محلولة (A4)؛ `spinValue` من `SpinController.get_SpinFactor`.
5. الكسر/الاقتراح بيستخدم المحرك التقريبي القديم — ده **اقتراح**، الخط الصلب هو الحقيقة.

## أول تجربة (٣ خطوات)
1. ركّب الـAPK (مش فوق القديم: احذف القديم الأول)، "تثبيت + إعادة تشغيل"، ادخل ماتش أوفلاين.
2. صوّب، اسحب الباور وسيب الإيد ثانية: لازم يظهر خط صلب.
3. "حفظ اللوج" وابعتلي `Download/pp_log.txt`. أهم أسطر: `[H]` (إيه اتحل/MISSING)، `[G]` (كام callback اتكتم)، `[L]` (كل حساب: ticks / ms / truncated / contact)، `[P]` (الأداء)، `[10]` (قيم التصويب).

## v84.1 — إصلاح "الأداة مشغلتش محاكي اللعبة" (2026-11-01)
سبب اللوج `[L] exact path not run: Physics.simulationMode != Script`: الـScript mode في اللعبة بيتحط على **السيرفر بس** (`ServerTracker.OnNetworkSpawn`, P2P)، والجهاز كان FixedUpdate فـUnity بيتجاهل `Physics.Simulate`. الحل: `Physics.set_simulationMode` (موجود في dump.cs) — الأداة بتحوّل لـScript جوه نفس الـcall المتزامن بتاع الحساب وترجّع القيمة الأصلية فورًا (RAII + من handler الـSIGSEGV). لو الـsetter مش متاح بترفض زي الأول. اللوج هيطلع مرة: `[L] Physics.simulationMode was 0: switching to Script...`. اتجرّب بالاختبارات بس (host)، مش على الجهاز.

## v85 — زر عايم للإعدادات
الشاشة الرئيسية بقت: تثبيت + إعادة تشغيل اللعبة، إزالة، الزر العايم، تحسين اللعبة/إلغاؤه. **كل الإعدادات + حفظ اللوج + التشخيص** جوه لوحة الزر العايم (`Panel.kt`, `FloatingService.kt`). الزر لازم إذن "العرض فوق التطبيقات": الأبّ بيمنحه بالروت (`appops set ... SYSTEM_ALERT_WINDOW allow`) وإلا بيفتح صفحة الإذن. الزر بيتشغّل تلقائي بعد "تثبيت + إعادة تشغيل". **الكود Kotlin ما اتبناش هنا (مفيش SDK/شبكة)؛ اتراجع يدويًا — لو البناء فشل ابعتلي سطر الخطأ.**
