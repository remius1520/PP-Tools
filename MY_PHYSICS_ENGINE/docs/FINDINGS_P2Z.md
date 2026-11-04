# P2Z — بند 7: تشغيل PhysX بتاع اللعبة نفسها (بدل إعادة كتابته)

## اللي اتكتشف (من dump.cs + libil2cpp، موثّق المصدر)
- الخطة الأصلية (مشهد فيزياء مخفي) **مش ممكنة بالشكل ده في البناء ده**: `SceneManager.CreateScene` و`PhysicsScene.Simulate` متشالين (stripped) من dump.cs. الموجود: `Physics.Simulate(float)` (الـdefault scene بس)، `Physics.Simulate_Internal(PhysicsScene,…)` (internal)، `MoveGameObjectToScene`، `LoadSceneAsyncNameIndexInternal`.
- لكن اللعبة شغّالة `Physics.simulationMode = Script` (P2P) ⇒ **مفيش حاجة بتحرّك الفيزياء غير نداء اللعبة لـ`Physics.Simulate`**. فنقدر وقت التصويب (الكور واقفة): نحفظ حالة كل الكور → نحط قواعد الضربة → ننادي `Physics.Simulate(dt)` tick بـtick → نسجّل المسارات → نرجّع الحالة. المحرك والمواد والكوليدرز = بتوع اللعبة الحقيقيين، فبيشتغل على **كل الترابيزات** من غير ملفات هندسة.
- libil2cpp بيصدّر API (`il2cpp_class_from_name`, `il2cpp_runtime_invoke`, `il2cpp_class_get_method_from_name`, …) ⇒ كل حاجة بتتحل **بالاسم** من غير RVAs.
- أسماء متحقق منها في dump.cs: `Rigidbody.{get/set}_{position,rotation,linearVelocity,angularVelocity}`, `Sleep/IsSleeping/WakeUp`, `Physics.{Simulate(1), OverlapSphereNonAlloc(5), SphereCast(6), get_simulationMode}`, `Collider.{get_attachedRigidbody,get_isTrigger}`, `Component.get_tag`, `GameManager.{get_Instance,get_Balls,<isHit>k__BackingField(+0xAC)}`, `Ball.get_Body`, `CueBall` (حقول +0x218/+0x220/+0x22C)، `GamePhysics.physicsBuilder(+0x20)`, `PhysicsBuilder.{forceDirection(+0x28),targetBallFinalVelocity(+0x44)}`, `SpinController.get_SpinFactor`. الـnamespaces: `PoolLiveTourMax.{Managers,Gameplay.Balls,Physic.Handlers,Gameplay.Shot}`.

## اللي اتبنى (src/pp2_live.hpp، android/pp2_il2cpp_host.{hpp,cpp})
- `live::predict(Host&, ShotIn, Options)`: snapshot (RAII، بيرجّع حتى لو فشل) → المسار الخاص P2Q/P2R (v=(baseline+vec)−pos كل tick، flag108 ⇒ 7 sub-steps) → ApplySpin عند أول تلامس غير-Table لو الجسم = collidedBall المتوقّع (بعد حل PhysX) → كشف الجيب (OverlapSphere على trigger "PocketInside") + إخراج الكورة → تسجيل frames → ميزانية وقت (250 ms) → restore.
- `il2::GameHost`: ربط حقيقي بـil2cpp. `init()` = فحص جاف (بيحلّ الأسماء ويقول OK/MISSING من غير ما يغيّر حاجة). `refresh()` بيرفض لو `simulationMode≠Script` أو `isHit==true` أو مفيش CueBall.
- الاختبارات: `test_live` (24 فحص، host وهمي 2D: الاسترجاع bitwise، المسار الخاص، ApplySpin مرة واحدة، جيب، ميزانية، فشل Simulate) و`test_il2cpp_host` (7 فحوص ضد API وهمي: بيفشل بأمان).
- **ده اختبار تحكّم/تدفّق، مش دقة فيزياء.** الدقة بتتقاس بس على الجهاز.

## افتراضات / مخاطر (لازم تتحسم على الجهاز)
- A1 كل ده ما اتجرّبش على لعبة: لا بناء NDK ولا تشغيل.
- A2 **Callbacks**: `Physics.Simulate` بيبعت OnCollision/OnTrigger للّعبة. أثناء التصويب `isHit=false` فـ handlers الجيب/ApplySpin متحرسة (P2U)، لكن الصوت/الأحداث التانية ممكن تتنفّذ. لو ظهر صوت/تأثير وقت التوقّع ⇒ محتاجين نكتم (مفيش setter لـ`invokeCollisionCallbacks` في dump).
- A3 استرجاع PhysX مش بالظبط 100% (warm-start/contact cache)؛ الكور واقفة فالأثر المتوقّع صغير بس مش مقاس. NetworkTransform: كل حاجة جوه نداء واحد synchronous فمفيش network tick بينها (افتراض).
- A4 `ShotData.SpinApplied` (spinUsed) لسه مش محلولة ⇒ false (قاعدة zero-spin متقفلة لحد ما تتحسم). F = |forceDirection| على xz (كيو مسطّح).
- A5 كشف أول تلامس بـOverlapSphere (نصف قطر r+2·contactOffset) بعد كل Simulate ⇒ ممكن يتأخّر/يتقدّم tick واحد عن `OnCollisionEnter` الحقيقي.
- A6 `ballRadius` لازم يتظبط حسب الترابيزة (kTables: 0.0400/0.0325/0.0335/0.0275/0.0275/0.0235)؛ ممكن يتقرا من SphereCollider × scale في خطوة جاية.
- A7 كشف الـ"Table"/"Edge"/"PocketInside" بالـtag كنصوص عادية (P0: a.dx()="Edge"، a.nn()="Ball").
- A8 لازم يتنادى من main thread.
## الخطوة على الجهاز
1) ركّب libpp2 وشغّل `GameHost::init` ← ابعت الـreport (أي MISSING بيتصلّح بالاسم). 2) توقّع ضربة فاضية على ترابيزة وقارنها بالضربة الحقيقية. 3) كل ترابيزة (6) نفس الخطوات.
