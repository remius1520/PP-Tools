# FINDINGS P2R (2026-10-31) — بند 4 (الباقي): المسار الخاص في OnServerSimulate اتفك واتنفّذ في المحرك
المصادر: `libil2cpp.so` (ARM64) + `dump.cs` (أسماء/offsets). `libunity.so` ما اتفتحش في الجزء ده. **مفيش مقارنة بضربة مرجعية** ⇒ "موثّق المصدر" مش "مطابق".

## 1) اللي اتحسم (من ARM64 + dump.cs)
- **دوال البداية التلاتة** (0x5DB70C0 / 0x5DB8BF0 / 0x5DB9018) = `ServerTracker.OnRequestShotServer` + نسختين obfuscated (`iie`, `gkr`) بنفس التوقيع `(clientid, forceDirection, collisionDir, spinvalue, targetFinalvel, trgetVeloc, stickNetid, itemdata, deafultcue)`. التلات نسخ بيكتبوا نفس الحقول بنفس الترتيب (متقارنين على مستوى الـstores) ⇒ **مفيش فرق منطقي**. اللي بينادي عليهم = معالجات RPC (`mpo`/`dlv`/`cni`…) — ما اتتبّعش.
- **حقول ServerTracker** (بتتكتب وقت طلب الضربة؛ dump.cs مش بيعرضها لأن الحقول 0xB0–0x110 مش ظاهرة فيه، فالمعنى من الـstores + معاملات الدالة):
  | offset | القيمة | المصدر |
  |---|---|---|
  | +0xB8 | spinvalue (float) | s6 |
  | **+0xBC..+0xC4** | **`vec` = forceDirection (Vector3)** | s0..s2 |
  | +0xC8..+0xD0 | collisionDir | s3..s5 |
  | **+0xD4..+0xDC** | **`baseline` = `cueRb.position` لحظة الطلب** | `Rigidbody.get_position` (0x5DB71C8) |
  | +0x108 | **SphereCast flag** (نتيجة `CueBall.OnCastCueBall`) | 0x5DB7218 → strb 0x5DB72F8 |
  | +0x110 | onSimulate = **true** (بعد `OnInitalizeSimulation` 0x5DB7390، في كل طلب ضربة) | strb 0x5DB7334 |
  (+0xE0..+0xEC = باقي معاملات الشوت (targetFinalvel/trgetVeloc من الستاك) — **مش متحقق**.)
- **`OnCastCueBall` (0x5DD6BB8)** = `Physics.SphereCast(origin = cue.position, radius = Ball.radius(+0xB4) + 2·defaultContactOffset, dir = normalize(vec) [لو |vec|≤1e-5 ⇒ static vector], hit, maxDistance = |vec|, layerMask = singleton+0xF8, queryTriggerInteraction = UseGlobal)` — اتأكدت من الـdisassembly كامل.
- **الشروط (0x5DB8618–0x5DB8638):** المسار الخاص بيشتغل لو `cueball != null && !cueball.insidePocket(+0x10B) && !cueball.InTube && !cueball.collided(+0x26C) && onSimulate(+0x110)`. غير كده ⇒ `if (!isSolving) Simulate(fixedDelta)` **مرة**.
- **`CueBall.collided` (+0x26C)** بيتكتب true في `CueBall.OnCollisionEnter` (0x5DD9B7C، مرة واحدة، السيرفر بس، لما `gameManager+0xAC ≠ 0`) عند **أول تصادم مع أي collider تاجه مش `a.uj()`= "Table"** (Edge/Ball/Pocket/Cutout…)؛ ولو `collidedBall(+0x218) != null` ⇒ `SyncSpin(coll)`.
- **المسار الخاص كل FixedUpdate:** `target = baseline + normalize(vec)·|vec|` (float32، Normalize/Magnitude = 0x5DC7888/0x5DC77C0)، `cueRb.linearVelocity = target − cueRb.position`، وبعدين:
  - **flag108 = true:** `ceil(substeps)`=7 نداء `Simulate(fixedDelta/7)` (float32) — السرعة بتتحط **مرة واحدة** في أول الـtick (مش كل sub-step).
  - **flag108 = false:** لو `|target − pos| < stopSimulateThreshold` ⇒ `position = target`, `v = 0`, `onSimulate = false`؛ بعدين `Simulate(fixedDelta)` مرة.
- **معنى ده فيزيائيًا (استنتاج من الكود):** البيضا قبل أول تلامس **مش بتتحرك بسرعة ابتدائية + damping**؛ سرعتها = المسافة المتبقية لهدف على بعد F (v(s) = F − s على خط الضرب، ثابت زمني ≈ 1 ث). ده بالظبط اللي بيفسّر صيغة `PredictedFinalVelocity = F − hitDistance` عند الكلاينت (اتساق مش مقارنة). الحالة بدون عائق: البيضا بتقف عند `baseline + vec` بالظبط (snap)، **مش** عند F/0.62.

## 2) اللي اتغيّر في المحرك (`src/pp2_engine.hpp`)
- `Cfg.special = true` (الافتراضي). `false` ⇒ السلوك القديم بالظبط (CLI `--no-special`).
- `Sim::castHits` = الـSphereCast (الكاست بنصف قطر `R + 2·contactOffset` ضد الكور (نصف قطر R) وضد الحيطان `W.sweep` والجيوب `pocketEntry`) ⇒ `Result.flag108`.
- حلقة `run()` بقت بـtick: tick عادي = خطوة `float32(fixedDelta)`؛ tick خاص + flag ⇒ 7 sub-steps؛ **بعد أول تلامس للبيضا (أي حيطة/كورة/جيب) ⇒ خطوة واحدة 0.01 لكل tick** (كان 7 sub-steps دايمًا). مسار الـpath بيتسجّل كل tick.
- أثر قياسي (مش مقارنة بلعبة): ضربة F=0.5 على ترابيزة فاضية: النهاية 0.5000 (كانت 0.8025). ضربة F=3 ناحية الحيطة: سرعة التلامس = F − s (1.92 م/ث) = اللي بيتنبّأ بيه الكلاينت.
- اختبارات: `tests/test_special.cpp` (13 فحص اتساق: snap على الهدف، عدد الـticks من متتالية مستقلة، v=F−s <0.5%، castHits، timestamps = k·fixedDelta). `build.sh` بيشغّله.

## 3) افتراضات / اللي لسه مفتوح (مكتوب، مش صامت)
1. **layerMask الكاست — اتحسم بالاستبعاد (مش متأكد من الـmetadata usage):** `ldr w1,[singleton,#0xF8]` (0x5DB71FC). الـsingleton من static slot 0x74DF3E0؛ الكلاس الوحيد في dump.cs اللي عنده `LayerMask` عند 0xF8 وهو singleton هو **`SpinController.spinLayer`**، وقيمته المتسلسلة من level5 = **0xC00 = layer 10 (BallBase = الكور) | layer 11 (Edge = الحيطان)** (`data/spin_controller_level5.json`)، وده بيتماشى دلاليًا (كور + حيطان). **مش داخل:** الـlayer 0 (سطح الترابيزة/الجيوب mesh K/D). المحرك: `Cfg.castMask = 0xC00` (`sweep(..., layerMask)`)؛ الجيوب (triggers) **برّه** افتراضيًا (`castPockets=false`) — layer الـCheckBall triggers مش مؤكد من الملفات اللي معايا (محتاج `scene_sharedassets0.json`/data.unity3d). تأكيد هوية الـslot 0x3E0 = `Singelton<SpinController>` من MethodInfo/metadata usage لسه ما اتعملش (الـslot بيستخدمه StickInput/CueBall/PhysicsBuilder/ServerTracker… ⇒ متسق مع إن الكل بينادي SpinController.Instance).
2. `engine.cueCollided` بيتعمل على أي تلامس للبيضا مع حيطة/كورة/جيب؛ اللعبة بتستثني "Table" بس — الـengine مفيهوش سطح ترابيزة كـcollider (Y مثبّت)، فالمعادل مقبول لكن **مش متحقق** لـcolliders تانية (Cutout).
3. حارس `gameManager+0xAC` في OnCollisionEnter (= "الضربة شغالة"، بيتكتب true في OnInitalizeSimulation 0x5DB7454) — ما اتحاكاش (افتراض إنه true طول الضربة).
4. ترتيب الـdamping جوه الـSimulate (linearDamping 0.62) لسه افتراض (`--drag-mode`) ⇒ بند 7 (libunity). فرق خطوة 0.01 مقابل 7 sub-steps بعد التلامس من الرتبة الثانية: تقديريًا ≈ x²·0.43 ≈ 1.6e-5 لكل tick (x=0.62·0.01) — **حساب نظري مش مقاس**.
5. معنى +0xE0..0xEC، والـbytes اللي بتتصفّر عند +0x109..0x10F في دوال البداية — مش متحقق (ما بتأثّرش على المحاكاة على حد علمي).
6. شروط الدخول في OnRequestShotServer (RemainingTime > 0.5، `gameManager+0xAC == 0`، إلخ) ما اتحاكتش — دي منطق قبول الضربة مش فيزياء.
7. **بند 5 — `Euler(spin).y` اتحسم من الباينري (جزئيًا: هوية atan2 من شكل الأرجومنتات):** `Quaternion.Internal_ToEulerRad_Injected` = icall 0x550EB4 (مسجّل عند 0x560D30) ⇒ normalize ⇒ `0xDD710C(q, order=4)`. فرع غير-gimbal (`|yz − xw| < 0.4999995f` = 0x3EFFFFDE): **y = atan2(2(xz+yw), ww+zz−xx−yy)**، x = −asin(2(yz−xw))-النوع، z = atan2(2(xy+zw), ww+yy−zz−xx). لدوران Y بس (x=z=0) الشرط gimbal = 0 فمش بيتفعّل، فـ**y = الزاوية نفسها ملفوفة لـ(−π,π]، مش صفر عند 180°** (عند 180° y ≈ −π ⇒ بعد MakePositive ≈ 180°). يبقى `Euler(spin).y ≠ 0` ⇔ `SpinAngelDeg ≠ 0` (اتجرّب على شبكة 0.1..360 في float32) ⇒ **افتراض المحرك سليم**. `toEulerYRad`/`eulerYDegPositive` في `src/pp2_clientpredict.hpp` + 4 فحوص في `tests/test_clientpredict.cpp`. غير متأكد منه: (أ) إن دالتي x9/x21/x20 (GOT مش مفكوك) = asin-مضروب/atan2f/atan2f (استنتاج من الشكل)؛ (ب) MakePositive بيتحسب من معرفة عامة (عتبة −0.0001) — الشرط `≠0` مش بيتأثر بيها.
8. **`CalcMaxSpinValues` الكاملة:** ما اتفتحتش في الجزء ده (دالة il2cpp مش libunity؛ الملاحظة في المصفوفة كانت إنها محتاجة libunity للدوال الرياضية بس). لسه مفتوح لو في حاجة ناقصة بعد المراجعة.
