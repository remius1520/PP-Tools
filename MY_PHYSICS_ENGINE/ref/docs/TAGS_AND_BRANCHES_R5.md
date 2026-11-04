# حسم التاجات وفروع التصادم (فك ساكن من libil2cpp.so + global-metadata.dat)

## فك نظام تشفير النصوص (class `a`, TypeDefIndex 11721) [مؤكد]
- الـblob: `global-metadata.dat` عند offset `0x94D798`، طول `0x4FE3`.
- فك الـblob (من `.cctor` عند 0x5DF463C): `out[i] = in[i] ^ (i & 0xFF) ^ 0xAA`.
- كل getter (`a.dx()` وغيره) = `a_(idx, offset, len)` = `UTF8.GetString(out, offset, len)`.
- طلعت **547 نص** في `pp2_decoded_strings.json` (مفتاحها = RVA الدالة).
- **`a.dx()` = "Edge"** (offset 3839، طول 4).

## TagConstant (static class) [مؤكد من dump.cs + الاستخدام]
| الحقل | offset | القيمة (مرجّح) | مين بيستخدمه |
|---|---|---|---|
| CUTOUT | 0x0 | "Cutout" | — |
| POCKET | 0x8 | "PocketInside" | SetCueDirectionAfterBallCollide (early return) |
| BALL   | 0x10 | "Ball" | PocketDebugger.OnTriggerEnter |
| EDGE   | 0x18 | "Edge" | CorrectCollisionRule.CheckForEdgeCollisions |
(قيم النصوص اللي في TagManager: Edge, Ball, PocketInside, Table, Cutout, Black, Scroll, Cuebase, CircleFoulCenter. "Pocket" لوحده مش موجود، فـPOCKET = PocketInside.)

## تصحيح الفروع (كانت "مش مؤكدة")
**PredictedFinalVelocity:** لو `collider.CompareTag("Edge")` → `f=|d|` (بدون معامل زاوية)، غير كده `f = d·clamp(|T(angle)|, minCue, 1)`. ثم clamp 0..20.

**SetCueDirectionAfterBallCollide(elasticDir, spinval, collider, cuebody):**
1. `collider != null` و tag == `POCKET(PocketInside)` → **return** (مفيش تغيير).
2. `s9 = spinval`; لو `zeroSpinThreshold(0.02) >= spinval` و `SpinController.SpinAppliedBool` → `s9 = 0`.
3. لو `collider.tag != "Edge"`: `cue.linearVelocity = 0`, `cue.angularVelocity = 0`, والسرعة النهائية `= s9`.
   لو `== "Edge"`: ما بيتصفّرش والسرعة النهائية = `spinval` (الأصلي).
4. `cue.linearVelocity = normalize_xz(elasticDir) * s`  (y=0).
⇒ بعد ضرب كورة (مش حيطة): سرعة البيضا = قيمة SpinFactor المرسلة، بالاتجاه `CollisionDirection`. يعني **SpinFactor هو اللي بيحدد سرعة البيضا بعد التصادم** (مش PhysX).

## لسه مفتوح
- حساب SpinFactor/SpinAngle من موضع الـtip (SpinController): الحزمة الأخيرة فيها `pp2_spin.hpp` (جزئي).
- ترتيب التنفيذ في SolveCollisions/OnServerSimulate (B.7).
- الـfallback Vector3 عند +0xB8 (B.3) وtuple السجلات (B.4).
- ربط levelMaxPower=4.3 بـF (B.6): اللي اتأكد إن `OnChangeForce(val)` بيكتب `horizontalForce=val` مباشرة.
- محرك الدحرجة/الاحتكاك 3D (D).
