# مراجعة أمان التنفيذ — PP v85 (exact path على IL2CPP / Android ARM64)
**الحالة:** مراجعة كود + قراءة dump.cs/الباينري. **ولا حاجة هنا اتجرّبت على جهاز.** نجاح Host tests = اختبار تدفّق على Unity وهمي، مش دليل أمان (بند 6).

## 1) Physics.Simulate يحرّك إيه، واللي بيتحفظ إيه
- `Physics.Simulate(dt)` بيقدّم **كل الـRigidbodies الديناميكية في الـdefault physics scene** (مش الكور بس) ويبعت callbacks لكل الـcolliders.
- **المحفوظ/المسترجع فعليًا** (`live.hpp::Guard`, `unityhost.hpp::get/set`): الأجسام في `GameManager.balls` بس (16 = الكيو + 15): position, rotation, linearVelocity, angularVelocity, IsSleeping (+ Transform.position/rotation لنفس الأجسام).
- **غير محفوظ (خطر):** (أ) أي كورة خارج القائمة (الجيب/الـtube) وأي Rigidbody تاني في المشهد؛ (ب) حالة PhysX الداخلية (contact cache, warm-start, sleep timers) — الكور بتتحط awake بعد الاسترجاع إلا لو كانت نايمة قبل؛ (ج) حالة interpolation؛ (د) `Rigidbody.Sleep` بيتنادى بس لو الكورة كانت نايمة. **ما اتأكدش** إن فيه Rigidbodies تانية ديناميكية في المشهد (محتاج dump للمشهد على الجهاز).
- الفحص قبل الحساب: `isHit==false` + سرعة **خطية** <0.02 فقط؛ **السرعة الزاوية مش متفحصة**.

## 2) Callbacks/آثار جانبية وقت المحاكاة
- Simulate بيطلق `OnCollision*/OnTrigger*` (ApplySpin، صوت، أحداث جيب...). المنع الحالي (`installGates`): استبدال `methodPointer` بالاسم لـ`OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit` ذات parameter واحد في Assembly-CSharp، ويرجع فورًا لو `g_inPredict`.
- **حدود المنع:** سقف 32 ميثود (`MAXG`) — لو فيه أكتر بيفضل الباقي من غير gate؛ assemblies تانية/مكتبات (Netcode, FMOD...) مش مغطاة؛ أي استدعاء مباشر من IL2CPP بيتخطى الـpointer؛ **الإعداد `gate=0` موجود في الواجهة** ولو اتقفل والـexact شغال = ApplySpin/صوت مزدوج محتمل؛ الاعتماد على إن handlers الجيب/ApplySpin محروسة بـ`isHit` (P2U) استنتاج من الكود مش مجرّب.
- **لازم يتأكد من اللوج:** سطر `[G] gated N callbacks: ...` (مش موجود في اللوج اللي وصلني). N==32 = على الأرجح ناقص.
- آثار تانية: `park()` بيحرّك كورة الجيب لـ(0,-100,0) وبيرجّعها؛ OverlapSphere/SphereCast قراءة بس.

## 3) استرجاع simulationMode
- **المسار العادي مضمون:** التحويل بيحصل **بعد** كل شروط الرفض؛ الاسترجاع: صريح بعد `predict` + RAII (`SimModeGuard`)؛ NDK بيبني بـexceptions مفعّلة (CMake من غير `-fno-exceptions`) فالـRAII بيشتغل مع exceptions (مش مجرّب).
- **الـhandler نفسه (`onSegv`) ما بيستدعيش Unity/IL2CPP** — `siglongjmp` أو chain بس. لكن **بعد** الـlongjmp كود الاسترجاع بينادي Unity (استرجاع الكور — من v84 — واسترجاع الـmode)، وده في سياق عادي مش داخل الـhandler، **بس بعد fault ممكن يكون جوه Unity/PhysX** فالحالة مش مضمونة.
- **ثغرة لقيتها في تعديلي السابق (v85):** الـmode كان بيتسترجع **بعد** `emergencyRestore` في نفس الـblock؛ لو الأخير فشل بـfault تاني الـmode يفضل Script ⇒ فيزياء اللعبة (FixedUpdate) تقف. **اتصلّحت:** الـmode الأول وفي guard لوحده، + verification بالقراءة (3 محاولات)، + **watchdog كل فريم** يعيد المحاولة حتى لو الميزة اتقفلت، + فشل الاسترجاع يقفل الـexact path ويسجّل `[X]`.
- **غير مغطّى:** fault على **thread تاني** (PhysX workers) ⇒ الـhandler ما بيعملش longjmp (فحص thread id) ⇒ بيعدّي لـhandler Unity ⇒ crash للعبة. وبعد longjmp من جوه Simulate ممكن PhysX يفضل locked/نص-خطوة: الحالة بعدها مش موثوقة (الـexact path بيتقفل لكن اللعبة بتكمّل).

## 4) تقدّم مزدوج / تغيير حالة اللعبة
- مفيش تقدّم زمني مزدوج للـ"game state": الحساب متزامن جوه LateUpdate على الـmain thread، الـFixedUpdate ما بيتداخلش، ومفيش حقل managed بيتكتب (isHit قراءة بس).
- لكن العالم الفيزيائي **اتقدّم فعليًا ثم رجع جزئيًا**: الأثر الباقي = بند 1 (غير المحفوظ). بالتالي "حالة اللعبة الأصلية مضمونة 100%" **مش** مثبتة.
- لو الجهاز Script أصلًا (سيرفر) السلوك زي v84 بالظبط (مفيش تحويل).

## 5) اللوج الجديد (اتضاف)
`[M] simulationMode before=0 -> set Script, reads 2 (switch #N)` ثم `[M] simulationMode after restore=0 (wanted 0) VERIFIED [after predict] switches=N fails=0`. فشل: `[X] simulationMode restore FAILED [...]`. (أول 20 ثم كل 10.)

## 6) اللي الـHost tests ما بتثبتوش
ABI/التواقيع الحقيقية لـ`set_simulationMode(enum)` و`Simulate` وقت التشغيل، إن الـsetter مش stripped/inlined في البناء ده (موجود في dump.cs، **ما فحصتش تنفيذه في libunity**)، سلوك Unity لما الـmode يتغيّر أثناء اللعب، حالة PhysX بعد الاسترجاع، الـcallbacks الحقيقية، الأداء/الهنج، ثبات الـmethodPointer patching.

## جدول المخاطر
| # | الخطر | الأثر | الحالة |
|---|---|---|---|
| R1 | Rigidbodies غير محفوظة تتحرك | كورة/جسم يتزحزح بعد التصويب | **مفتوح** (محتاج فحص مشهد) |
| R2 | callbacks خارج الـgate (سقف 32/assemblies/gate=0) | صوت/ApplySpin/أحداث | **مفتوح** — اقرا `[G]` |
| R3 | fault جوه Simulate ثم استدعاء Unity | حالة فيزياء تالفة/كراش | **مخفّف جزئيًا**، مش مُلغى |
| R4 | الـmode يفضل Script | فيزياء اللعبة تقف | **اتصلّح + watchdog + verify** |
| R5 | fault على worker thread | كراش | **غير مغطّى** |
| R6 | سرعة زاوية غير متفحصة | حساب والكور لسه بتلف | صغير، **مفتوح** |
| R7 | setter مش مفحوص في libunity | تحويل ما يعملش/يعمل أثر | **مفتوح** — اللوج `[M]` هيكشفه |

## بناء الـAPK
**ما اتبناش** — مفيش Android SDK ولا شبكة هنا؛ كود Kotlin (Panel/FloatingService/MainActivity) ما اتكمبلش أبدًا. الـvariant المنفصل جاهز: `applicationId=com.premiumpath.test`، الاسم "PP TEST"، `versionName v85-safety-test`، مسار الـlib `/data/local/tmp/pp_test` (V84 الأصلي ما اتلمسش). **تحذير:** الاتنين بيشاركوا `wrap.com.PoolLiveTour` وملفات `pp_cfg/pp_log` في مجلد اللعبة، فآخر "تثبيت" هو اللي بيتحقن. البناء: ارفع المجلد على GitHub (workflow جاهز: `gradle assembleDebug` ⇒ artifact `premiumpath-test-apk`).

## خطة تجربة على الجهاز (بالترتيب، أوقف عند أول `[X]`)
1) `[H]` كله OK و`[G] gated N<32`. 2) أول ضربة تصويب: `[M] before=0 … reads 2` ثم `VERIFIED`. 3) نفّذ ضربة حقيقية بعدها: الكور لازم تتحرك عادي. 4) راقب صوت/حركة غريبة وقت التصويب. 5) قارن مواضع الكور قبل/بعد التصويب. 6) ابعت `pp_log.txt`.
