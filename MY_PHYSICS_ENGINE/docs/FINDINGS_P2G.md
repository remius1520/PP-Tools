# FINDINGS P2G — حلّال التداخل + SyncSpin/ApplySpin (مراجعة مستقلة، 2026-10-31)

## الجزء B — الحلّال (PhysicsBuilder.SolveBalls وأولاده): **اتأكد، مطابق لـ`src/pp2_solvers.hpp`**
فكّيت الدوال الأربعة تاني بشكل مستقل من ARM64:
- `SolveBalls` (0x5D40FA4): `co = Physics.defaultContactOffset`؛ `safeval = 0.00012f` (rodata 0x1319B38)؛ `solverValue = 2·co + safeval`؛ `edgeSolver = co · 0.6f` (0x1318940). لكل كورة مش InTube: `SolveWithCueBall` ثم `SolveBallWithCushions`، وبعدين `SolveBallWithBall` مرة. **ممرّة واحدة بس لكل نداء.**
- `SolveWithCueBall` (0x5D3F86C): `gap = |cue−pos| − 2·cue.radius(+0xB4)`؛ `gap > solverValue` ⇒ بدون تغيير؛ `push = (gap==solverValue ? safeval : 0) + solverValue − gap` على اتجاه `(pos−cue)/|..|` (احتياطي: static Vector3 لو الطول ≤ 1e-5). بتتخطى الكورة نفسها (`ball != cue`) ولو `RulesManager.Foul`.
- `SolveBallWithCushions` (0x5D3FB10): لكل collider في قائمة الحيطان (`+0x50`): `closest = collider.ClosestPointOnBounds(pos)`، `gap = |pos−closest| − ball.radius`، `gap > edgeSolver` ⇒ تخطّي، نفس صيغة الدفع، والموضع بيتحدّث بين الـcolliders.
- `SolveBallWithBall` (0x5D3FE64): كل زوج i<j مش InTube، `dist < 2r` ⇒ كل كورة بتتحرك `(2r−dist)/2` (واحدة −dir والتانية +dir)، `dist < 1e-5` ⇒ `Random.insideUnitSphere` (مش حتمي).
- **حلقة `solvecount` (+0x148 في ServerTracker، 8 في اللعبة حسب الملف الأم):** مش في الفيزياء، هي في كوروتين `ServerTracker.<SolveBalls>d__41.MoveNext` (0x5DBA5E4): كل مرور = `OnSolveBall()` (يطبّق `transform.position = newPos` لكل كورة) ثم `yield return null` (يعني **إطار لكل مرور**)، وبعد الحلقة `Rigidbody.Sleep()` على كل الكور الحيّة.
- **الكوروتين بيتبدأ من `ServerTracker.SleepBalls()` (0x5DB8854) بس** ⇒ حلّال التداخل **مش بيشتغل وقت الضربة**؛ هو تنضيف مواضع لما الكور تهدى. **مالوش أثر على التنبؤ بالضربة** (إلا لو الكور متداخلة قبل الضربة).

## الجزء A — SyncSpin / ApplySpin (CueBall): فك جزئي
- `CueBall.SyncSpin(Collision)` (0x5DD8270): لو `collision.gameObject == this.lastCollided(+0x218).gameObject`: `TryGetComponent<Ball>`؛ لو شرط الـsingleton (4b1af70) اتحقق ⇒ `ApplySpin(collidedBall)`؛ بعدين بيمسح `+0x210/+0x218`.
- `CueBall.ApplySpin(Ball)` (0x5DD83B4): بينادي **`HandlePhysicsCollisions.SetTargetBallAfterCollision(targetRig, hitMagnitude, elasticDir)`** (0x5D3B350) ثم **`SetCueDirectionAfterBallCollide(elasticDir, spinval, collider, cuebody)`** (0x5D3B964) — الاتنين هم الدوال اللي V5 فكّها في `pp2_scripted.hpp`. **التأثير الفعلي للسبين = اختيار `spinval`/`elasticDir`** اللي بتتبعت لهم، مش معادلات جديدة.
- **[اتحسم]** الكلاس المجهول = **`ServerTracker`** (الـslot 0x7502608 بيستخدمه `GameManagerService.OnPingBallVelocity` اللي بينادي `ServerTracker.SleepBalls` على نفس الكائن). السجل الجاهز = **`ServerTracker.serverShotData` (struct `ShotData` @+0xA8)**، وحقوله (offset داخل الـstruct + 0xA8): `spinValue` +0x10→**0xB8**، `ForceDirection` 0xBC، `CollisionDirection` 0xC8/0xCC/0xD0 (x,y,z — y متجاهَل)، `startpos` 0xD4، `targetBallDirection` **0xE0/0xE4/0xE8**، `targetBallvelocity` **0xEC**، `tick` 0xA8، `isSpining` 0x108. يعني: `SetTargetBallAfterCollision(target, hitMagnitude=targetBallvelocity, elasticDir=targetBallDirection)` و`SetCueDirectionAfterBallCollide((CollisionDirection.x, ., .z), spinval=spinValue, ...)`.
- **النتيجة المهمة:** ApplySpin ما بيحسبش حاجة — بيطبّق قيم **جاية جاهزة من ShotData** (`NetworkSerialize`: بتتبعت من اللاعب للسيرفر). يعني **اتجاه الكورة البيضا بعد التلامس وسرعة/اتجاه كورة الهدف بتتحسب عند اللاعب (PhysicsBuilder: GetForceDirectionToHit / PredictCueDirAfterBallCollision / PredictedFinalVelocity)** والسيرفر بينفّذ. فالتنبؤ الصح = إعادة حساب نفس دوال PhysicsBuilder (مش حلّال السيرفر). الخطوة الجاية: مين بيكتب حقول `ShotData` عند اللاعب.
- (النص الأصلي قبل الحسم) مصدر الأرقام: 4 floats متجاورة (+0xE0..+0xEC) من كائن راجع من `Singleton<?>.Instance` (0x4B1AF70): `[+0xEC]` = hitMagnitude، و`[+0xE0],[+0xE4],[+0xE8]` = elasticDir، و`[+0xB8]`/`[+0xD0]` للنداء التاني. **هوية الكلاس لسه مش محسومة** (الـdump مش بيظهر كلاس بـVector3@0xE0 وfloat@0xEC؛ الأرجح NetworkVariable struct/غلاف). ده أهم باقي في A.

## لسه
1. تحديد كلاس 0x4B1AF70 وقيمه (متى بتتكتب elasticDir/hitMagnitude/spinval) — بيحسم تأثير السبين على السرعة والاتجاه.
2. ربط `SetTargetBallAfterCollision`/`SetCueDirectionAfterBallCollide` بـ`Sim::run` (V5 FINDINGS بند "لسه مجهول": مين بيملا الطابور، التوقيت في Update).

## [اتحسم] مين بيكتب ShotData: `StickService.ShotTheBall()` (0x5DCAAA4) → `Player.RequestShot(ShotData, Stick)` (0x5DA91F4)
ShotData بيتبني على الـstack عند اللاعب من **حقول live**:

| حقل ShotData | المصدر (فك ARM64) |
|---|---|
| `spinValue` | `SpinController.get_SpinFactor()` (0x5D9E598) = ناتج `applySpinFactorCombination` |
| `ForceDirection` | `GamePhysics.Instance.physicsBuilder.forceDirection` (PhysicsBuilder +0x28/2C/30) |
| `CollisionDirection` | **`CueBall.forceDirectionAfterCollided`** (+0x220) |
| `targetBallDirection` | **`CueBall.collidedBallForceDirection`** (+0x22C) |
| `targetBallvelocity` | **`PhysicsBuilder.TargetBallFinalVelocity`** (+0x44) |
| `startpos` | `cueBall Rigidbody.position` |

**الخلاصة المعمارية:** اتجاه البيضا بعد أول تلامس واتجاه/سرعة كورة الهدف بتتحسب عند اللاعب (CueBall/PhysicsBuilder) وتتخزّن في الحقول دي، والسيرفر (`ApplySpin` ← `SetTargetBallAfterCollision` / `SetCueDirectionAfterBallCollide`) بينفّذها كما هي. يعني **المحاكي لازم يعيد حساب دوال العميل اللي بتكتب `forceDirectionAfterCollided` و`collidedBallForceDirection` و`TargetBallFinalVelocity`** (PredictCueDirAfterBallCollision = cqo 0x5D40438، PredictedFinalVelocity 0x5D3DBE0 … وV5 `pp2_scripted/postcollision` غطّاها). الخطوة الجاية: تحديد كاتبي الحقلين `CueBall+0x220/+0x22C` ومقارنة فك V5 بيهم.
