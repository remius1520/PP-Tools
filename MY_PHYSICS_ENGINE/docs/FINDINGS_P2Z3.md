# FINDINGS P2Z3 (2026-11-02) — نهاية الضربة (Ball.IsMoving / BallInAction / SleepBalls) + حساسية PhysX sleep
المصادر: `libil2cpp.so` (ARM64، part01+part02) + `dump.cs` + `global-metadata.dat` + runtime dumps (`ref/runtime_dumps/`). `libunity.so` اتسلّم بس ما اتفتحش في الجزء ده (اتفحص بس بحث نصوص الـicalls). **مفيش مقارنة بضربة مرجعية ⇒ "موثّق المصدر" مش "مطابق".**

## 0) تأكيد مستقل لـP2Q/P2R
فكّيت `OnServerSimulate` (0x5DB856C) و`OnRequestShotServer` (0x5DB70C0) و`OnCastCueBall` (0x5DD6BB8) من الأول من غير ما أبص على P2R، والنتيجة نفسها:
- `FixedUpdate` (0x5DB99F4) = `if (IsServer) tail-call OnServerSimulate` (0x5DB99FC)؛ مفيش نداء تاني لـ`OnServerSimulate`.
- طلب الضربة بيكتب `ForceDirection`(+0xBC)، `startpos`(+0xD4 = `cueRb.position`)، **+0x108 = نتيجة `OnCastCueBall`**، **+0x110 = onSimulate = true**؛ والكاست: نصف القطر `Ball.radius(+0xB4) + 2·defaultContactOffset`، الاتجاه `normalize(vec)`، المسافة `|vec|`.
- المسار الخاص: `v = (startpos + normalize(F)·|F|) − pos` كل tick قبل `collided`؛ +0x108=true ⇒ `ceil(substeps)` نداء `Simulate(dt/substeps)`، false ⇒ snap لو `< stopSimulateThreshold` وبعدين `Simulate(dt)` مرة. بعد أول تلامس: `Simulate(dt)` مرة لكل tick (إلا لو `isSolving`).
- المعنى: **المحرك الحالي صح هنا**، ومفيش تغيير في الفيزياء بسبب البند ده.

## 1) نهاية الضربة — اللي اتحسم (من ARM64 + dump.cs)
| الدالة | RVA | اللي بتعمله |
|---|---|---|
| `GameManagerService.OnPingBallVelocity` | 0x5DC3A7C | لو `!BallInAction()` ⇒ `gameManager.isHit(+0xAC)=false`، `ServerTracker.SleepBalls()`، `UnityEvent(+0xC8).Invoke()`، `startListen(+0xB0)=false` |
| `GameManagerService.BallInAction` | 0x5DC3B04 | بتلف على `RulesManager._ballsLeft`(+0x138، `List<Ball>`) وبعدين `_cueBall`(+0xF0): الكورة الغير `InTube` اللي `IsMoving() \|\| IsOverlapping() \|\| IsBallInsidePocket()` ⇒ الضربة لسه شغالة |
| `Ball.IsMoving` | 0x5DD6618 | **`‖rb.linearVelocity‖ ≥ Ball.sleepThreshold(+0xC4)`** (`fcmp; cset ge` عند 0x5DD66AC، الطول 3D لكن Y مثبّت) |
| `Ball.IsOverlapping` | 0x5DD63C4 | `Physics.OverlapSphereNonAlloc(rb.position, Ball.radius, overlapResult(+0xC8), mask = Ball.gameManager(+0x198).gameManagerService(+0x1A8).OverLapLayer(+0xB8), QueryTriggerInteraction=2)`؛ بعدين فلترة للنتايج (rigidbody غير بتاع الكورة نفسها + حاجة بـtag) — **الفلترة ما اتفكتش** |
| `ServerTracker.SleepBalls` | 0x5DB8854 | `Rigidbody.Sleep()` لكل كورة في `gameManager.balls` مش `InTube`، وبعدين `StartCoroutine(SolveBalls())` |
| `ServerTracker.<SolveBalls>d__41.MoveNext` | 0x5DBA5E4 | بينادي `OnSolveBall` (0x5DB62A8) ⇒ `PhysicsBuilder.SolveBalls()` (0x5D40FA4) ⇒ `transform.position = …` لكل كورة (اتغطّى في P2: `solveOverlap`) |
- **`Ball.sleepThreshold` = 0.002f** في كل الـruntime dumps (الترابيزات 5/7/9/11، `0x3b03126f` عند +0xC4). يعني شرط الوقوف الحقيقي = كل الكور سرعتها < 0.002، نفس الرقم اللي المحرك كان بيستخدمه (من `stopSimulateThreshold`).
- مفيش حاجة في كود `Ball` بتكتب سرعة/sleep (`set_linearVelocity` بس في `SetBallState`/`dcs`؛ `Sleep` بس من `SleepBalls`/`SolveBalls`)، يعني الكور بتكمّل PhysX لحد ما `OnPingBallVelocity` تلاقيها كلها واقفة.

## 2) تصحيح لملاحظة قديمة
ملاحظة المحرك/الملف الأم كانت بتقول إن شرط الوقوف = `stopSimulateThreshold` عبر "cue-ball predicate 0x5DB79D4 على |linearVelocity|". الفك بيقول:
- `ServerTracker.IsCueBallMoving` (0x5DB79D4) + 3 نسخ obfuscated (`mje` 0x5DB9744، `kjh` 0x5DBA1F0، `nr` 0x5DBA488) بتقرا **`get_angularVelocity()`** (مش linear) وبتقارنها بـ`+0x144` بـ`>`؛ و`IsCueBallMoving` **مفيش ليها نداء مباشر** (bl/b scan على الباينري كله).
- الرقم نفسه (0.002) فمفيش تغيير عددي، بس المصدر الصح هو `Ball.sleepThreshold` والمقارنة `≥` مش `>`.

## 3) اللي اتغيّر في الكود
- `Cfg.ballStop = 0.002` (جديد، `Ball.sleepThreshold`)؛ `Cfg.stop` فضل لـsnap المسار الخاص بس. شرط "لسه بتتحرك" في `run()` بقى `‖v‖ ≥ ballStop` (3 أماكن + الأحداث `tev`)، وفي `src/pp2_live.hpp` (`Options.ballStop`)، و`pp2_set("ballStop")`، وCLI `--ball-stop`. القيم الافتراضية متساوية ⇒ **مفيش تغيير في أي ناتج** (كل الاختبارات القديمة بنفس أرقامها).
- `tests/test_endshot.cpp` (8 فحوص اتساق، في `build.sh`): الافتراضيات، إن `stop` ما بيأثرش على نقطة وقوف كورة حرة وإن `ballStop` بيأثر بـ`(thr−0.002)/drag`، حد `≥`، وإن snap المسار الخاص شغال.

## 4) مفتوح — أكبر مصدر شك دلوقتي في "الكورة بتقف فين": PhysX sleep (افتراض، مش من الباينري)
- اللعبة **مبتضبطش `Rigidbody.sleepThreshold` أبدًا**: `Rigidbody` في dump.cs مفيهوش `get/set_sleepThreshold` (الـmanaged wrappers اللي اللعبة بتستخدمها بس: `set_linearDamping/angularDamping/mass/constraints/...`)، و`IsSleeping()` ملهاش نداء غير `NetworkRigidbodyBase`. وفي نفس الوقت اللعبة بتعمل `WakeUp()` لكل الكور أول كل ضربة (`AwakeBalls` 0x5DB6C74) و`Sleep()` آخرها ⇒ الكور بتنام فعلًا بين الضربات.
- يبقى الـthreshold = `Physics.sleepThreshold` = **0.001** (من data.unity3d، `GAME_FILE_FACTS.md`).
- **من معرفة عامة بـPhysX/Unity (مش متأكد من libunity):** الجسم ينام لما `0.5·‖v‖²` (مقسومة على الكتلة) تفضل أقل من الـthreshold حوالي 0.4 ث ⇒ سرعة ≈ √(2·0.001) = **0.0447 م/ث**، بعدها بـ~0.4 ث الكورة تقف حتى لو لسه بتتباطأ. المحرك فيه `Cfg.sleepKE` (الافتراضي 0 = مقفول) لده.
- **الحساسية (قياس على المحرك، مش على اللعبة):** مشهد فيه 3 كور وضربة F=2.4: `--sleep-ke 0` ⇒ البيضا (0.6644, −0.3880) و`--sleep-ke 0.001` ⇒ (0.6568, −0.3334): **فرق 5.5 سم** (~2 نصف قطر)؛ كورة تانية 4.7 سم؛ زمن الضربة 10.9 ث مقابل 6.3 ث. يعني لو الافتراض صح، **كل موضع وقوف** في المحرك دلوقتي أبعد ~5 سم من اللعبة.
- الحسم (من غير تسجيل ضربات): (أ) **المسار الحي P2Z** (`Physics.Simulate` الحقيقية) بيحلّها لوحده لأن PhysX بتاع اللعبة هو اللي بينيّم؛ (ب) أو فك منطق الـsleep/wakeCounter في `libunity.so` (دالة `updateWakeCounter` في PhysX 4.1.2؛ ثابت 0.4 ومقارنة الطاقة) — P2Y ما لقاش دوال الـintegrate فمحتاج بداية من حاجة تانية (مثلاً مرجع `sleepThreshold` setter handler). لحد ما يتحسم: `sleepKE=0` (الافتراضي) **مش موثّق**.

## 5) افتراضات تانية (مكتوبة)
- `pingBallRate`(+0xA4)/`startListenTime`(+0xA0)/`safeballRate`(+0xA8)/`sleepThreshold`(+0xB4) في `GameManagerService`: قيمها في مشهد/prefab (مش في الـruntime dumps)؛ تأثير الـping على الموضع ≤ v/drag ≈ 3 مم (الكورة تحت 0.002 م/ث وقتها)، فمش مطبّق. الملف المطلوب: scene json فيه `GameManagerService`.
- شرط `IsOverlapping` (فلترة النتايج) بيطوّل الضربة لحد ما الكور تتفصل؛ المحرك بيطبّق `solveOverlap` بعد الانتهاء (مختلف في التوقيت، نفس الفكرة).
- `Ball.sleepThreshold` لترابيزة Lvl1_4 وLvl13: اتأكدت بس من dumps 5/7/9/11 (prefab واحد حسب P2X فالمتوقع نفسها).
