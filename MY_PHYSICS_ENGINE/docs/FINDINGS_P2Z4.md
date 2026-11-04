# FINDINGS P2Z4 (2026-11-02) — مسجّل الـtick (بند 1 من "اللي لازم يتقفل" في MASTER_37)
**الحالة:** اتبنى واتجرّب على stub بس (g++ هنا). **ما اتجرّبش على لعبة.** مفيش لوج جديد اتبعت في الجلسة دي، فمفيش مقارنة.

## اللي اتعمل
`android/pp2_il2cpp_host.{hpp,cpp}` (القراءة بس، مفيش كتابة في اللعبة):
- `GameHost::sampleTick(TickRec&, why)`: بيقرا `Time.fixedTime` + `Physics.simulationMode` + `isHit` + لكل كورة: pos / linearVelocity / angularVelocity / `isKinematic` / `linearDamping` / `IsSleeping`. بيفشل بأمان (`why`) لو أي نداء رجع null.
- `refresh(why, forLog=true)`: بيتخطى رفض `simulationMode != Script` ورفض `isHit` (المسجّل لازم يشتغل وقت الضربة). الاستخدام العادي (`forLog=false`) ماتغيّرش.
- `formatTick`: سطر واحد/tick بـ6 أرقام عشرية (اللوج القديم كان 5 وكل السرعات `0.00000`).
- `looksBroken`: بيرجّع true لو كل المواضع والسرعات صفر بالظبط ⇒ علامة قراءة Vector3 معطوبة، مش وقوف حقيقي.
- `init()` بقى يفحص كمان: `Time.get_fixedTime` و`Rigidbody.get_isKinematic` و`get_linearDamping` (**الأسماء من معرفة عامة بـUnity 6، مش من dump.cs** — لو ظهرت MISSING في probe الجهاز يبقى الاسم مختلف ونصلحه).
- `tests/test_il2cpp_host.cpp`: 7 ⇒ 12 فحص. `build.sh` كله نجح.

## لازم تعرفه (افتراضات)
1. **التوصيل جوه FixedUpdate مش في الزيب ده.** مفيش في الزيب كود الهوك اللي بيسجّل `pp2_shot_*.txt`. `sampleTick` لازم تتنادى من هوك على `ServerTracker.FixedUpdate` (0x5DB99F4 = `if (IsServer) → OnServerSimulate`) **بعد** ما يخلص، على الـmain thread. لو الهوك بتاع التطبيق بيشتغل على فريم الرسم هتفضل المشكلة. (استنتاج من P2Z3 §0.)
2. **سبب السرعات الصفر لسه مش معروف.** الاحتمالات: توقيع نداء غلط للـVector3 في المسجّل القديم (استنتاج)، أو القراءة بعد ما اللعبة تصفّر. `looksBroken` هيفرّق لما نشوف اللوج الجديد. الكود هنا بيقرا Vector3 بنفس طريقة `get()` (unbox 12 byte)، وده ما اتجرّبش على اللعبة.
3. سطر `hit=1` + `kin` + `damp` هيجاوبوا على بنود 2 و3 (وضع المحاكاة وkinematic البيضا) من أول لوج.

## الخطوة الجاية
ابعت لوج ضربة بيضا بس من المسجّل المعدّل (`pp2_shot_*.txt` + `pp2_probe_*.txt`)، وبعدها نقارن tick بـtick (بند 4).
