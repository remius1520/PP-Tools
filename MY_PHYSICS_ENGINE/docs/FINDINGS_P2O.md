# FINDINGS P2O (2026-10-31) — بند 1: ClientSpinController (من ARM64)

## محسوم (من libil2cpp)
- **الإدخال → h/v:** `ClientSpinController.SpinAnchorUpdate` (0x3599650): من delta الـInput System، `vertical(+0x3C) += dy·controllSpeed`، `horizontal(+0x40) += dx·controllSpeed`، وبعدها **clamp مربع**: `h ∈ [minX·D, maxX·D]`، `v ∈ [minY·D, maxY·D]` حيث D = `Stick.StickSpinFactorDividor` (0x5D9BD24) للستيك الحالي، و`radius(+0x44) = SpinController.constRadius(+0xB4)·D`. الدايرة بتتقصّ بس في مكان النقطة المرسومة (UI)، مش في h/v نفسهم.
  → **وحدات h/v = وحدات UI الدايرة** (±60·D)، و**radius = 60·D** (مش 60 ثابتة زي ما افترضنا في P2L).
- **Update** (0x359A674، للمالك بس): لو `|h| ≤ 1` و`|v| ≤ 1` ⇒ `SpinController.ResetSpinValues()` (لا سبين). غير كده ⇒ `GetAngelAndSpinFactorLimitions(h, v, radius)` (0x5DA3BD8) ثم `CheckSpinAngelDelta`. (`PointInThreshHold` 0x3597DF8 نفس الشرط ±1.)
- **`GetAngelAndSpinFactorLimitions`** (0x5DA3BD8): `currentDifference(+0x164) = |CueBall.intBallAngle − 90|` (يتكتب هنا بس — الافتراض القديم "=0" غلط)، بعدين `CalcMaxSpinValues(radius)`، وبعدين: لو `spinType(+0xD8) == 1` ⇒ `GetSpinAngelMapping(h, v)`؛ غير كده ⇒ `angleDeg(+0xC0) = ApplySpinAngelCombinationForEdge(h, radius)`؛ وبعدها `ApplySpinFactorCombination`.
- **spinType** (من `OnSpinAnchorChanged` 0x5D9FE28…): `CompareTag(collidedBall, "Ball")` ⇒ **1**، غير كده **2** (Edge)، و0 لو مفيش collider.
- **`ApplySpinAngelCombinationForEdge`** (0x5D9EA40): `ratio = clamp(|h|/radius, 0, 1)`، `a = intBallAngle > 90 ? 180−intBallAngle : intBallAngle`، الناتج `ratio · 0.5 · (h < 0 ? +a : −a)` (درجات، من غير Round). **ماكانش متنفّذ في المحاكي** (كان بيستخدم mapping الكرة للحيطة).
- **intBallAngle للحيطة:** `OnHandleCueDirection` بيحسبها لأي collider بنفس الصيغة (acos مع normal الحيطة) — المحرك دلوقتي بيحسبها للحيطة كمان (كان 0).
- ctor الـSpinController بيحط `constRadius = 74` كافتراض (0x5DA0C18) والمشهد بيكتبها 60 (serialized).

## الكود (تغييرات P2O)
- `spinwire::applyTip`: reset لو |h|,|v|≤1؛ clamp h/v بـ[min·D, max·D]؛ `radius = constRadius·D` (الافتراضي)؛ `currentDifference = |angleInt−90|` (`SceneCfg.useGivenDifference` للتجاوز)؛ `spinType` = 1 للكرة / 2 للحيطة؛ زاوية الحيطة من `edgeAngle(...)`؛ `intBallAngle` الحقيقي لكل من الكرة والحيطة (مش 90 للحيطة).
- `Prediction.ballAngleDeg/angleInt` اتملّوا لحالة الحيطة (مفيش تغيير في أي ناتج تاني).
- CLI: `tip <h> <v> [radius] [cueDividor]` (radius الافتراضي = 60·D).
- اختبارات `test_spinwire` بقت 8 (reset، صيغة edgeAngle، currentDifference، حيطة).

## أثر على ضربة بسبين (مهم)
لما الضربة على **حيطة** بسبين: زاوية السبين دلوقتي من `edgeAngle` (كانت من mapping الكرة)، و`spinType=2` بيغيّر maxima. ولما الضربة على كرة: `currentDifference` بقى غير صفر ⇒ بيغيّر SpinFactor/الزاوية عن P2L. (من غير مقارنة بضربة مرجعية: "موثّق المصدر" مش "مطابق".)

## افتراضات لسه مفتوحة
1. الستيك (D): الافتراضي 0.75 (الأكتر شيوعًا في cueitemsConfig)؛ `canSpin` (0x5DA0A3C: `CueItem.SpinFactorDividor>0` و`IsDefaultCue`) ما اتفحصش.
2. تأثير `ResetSpinValues` على SpinFactor (بنفترض = من غير سبين، SpinFactor=finalVelocity، زاوية 0) — الجسم ما اتفكّش بالكامل.
3. مدخل `OnUpdateSpinAnchor`→`spinPointAnchor` (NetworkVariable) والـ`OnSpinAnchorChanged` للسيرفر: مش لازم للمحاكي لأن اللاعب بيحسب محليًا، لكن ما اتأكدش إن السيرفر مبيعيدش الحساب.
4. هوية الـsingleton = GamePhysics: لسه استنتاج (P2L).
5. `h/v` بيتحدّدوا من الإدخال باللمس (delta × controllSpeed 0.3)؛ فمفيش وحدة "بكسل → UI" مطلوبة، بس الـtip لازم يتدخل بوحدات UI.
