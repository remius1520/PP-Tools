# FINDINGS P2L (2026-10-31) — بند 1: مدخلات السبين اتحسمت ومربوطة بالمحاكي

## محسوم (من ملفات اللعبة)
- **قيم SpinController المسلسلة** من مشهد `level5` (data.unity3d)، GameObject "SpinController"، سكربت pathID 4894، الجسم الخام متطابق مع offsets الـdump (جسم+0x74 = class+0x130): constRadius 60 · maxX/maxY ±60 · controllSpeed 0.3 · left/right/bottomIncreament = 1 · min/max base: increment 0.2/0.4، decrease 0.1/0.3، afterZero 0.17/0.37، side 0.25/0.45 · maxBaseIncreaseEdge 0.5 · maxBaseDecreaseEdge 0.6 · zeroDifferenceMultiplication 3 · deadZone 7 · deadZoneVertical 7 · classLimiters فاضية. الملف: `data/spin_controller_level5.json`.
- **globalBias = −0.22**: float في rodata libil2cpp عند VA 0x1318854 (اتحمّل في CalcMaxSpinValues 0x5DA0178)؛ مش runtime.
- **externalBaseline** = `GamePhysics.Instance → physicsBuilder(+0x20) → +0x40` = **PhysicsBuilder.finalVelocity**؛ والـvector (+0x28/+0x2C/+0x30 لنفس الكائن) = **PhysicsBuilder.forceDirection**؛ و"cueBallRadius" (CueBall+0x25C) هو في الحقيقة **CueBall.hitDistance**. (تطابق offsets الحقول مع dump.cs؛ هوية الـsingleton = GamePhysics **استنتاج** من إن نفس الـslot 0x74dba18 بيستخدمه كل مستهلكي PhysicsBuilder: Ball/CueBall/ShotTheBall/StickInput.. وإن الحقول +0x20/+0x28/+0x40 بتطابق PhysicsBuilder بالظبط.)
- النتيجة: كل مدخلات `evaluateSpinPipeline` بتتحسب من تنبؤ الضربة نفسه (finalVelocity، |forceDirection|=F، hitDistance) + قيم المشهد + قيمة الستيك (`data/cueitems_effective.csv`).
- فحص اتساق من كود اللعبة (مش fit): tip في المنتصف ⇒ SpinFactor = finalVelocity بالظبط (2.84681 = 2.84681)، وده بيطابق ملاحظة الملف الأم "من غير سبين SpinFactor = finalVelocity".

## الكود
- `src/pp2_spinwire.hpp`: `fromTip(...)` و`applyTip(Sim, balls, Shot&, Tip)` (بيتنبأ من غير سبين، يحسب الـpipeline، يكتب `Shot.spin` و`spinAngleDeg`). CLI: سطر `tip <h> <v> [radius] [cueDividor]` قبل `shot`. اختبار: `tests/test_spinwire.cpp` (4 checks) في build.sh.

## افتراضات (مش محسومة)
1. `currentDifference` (+0x164) = 0 (قيمة runtime؛ مين بيكتبها؟ SpinController — ما اتفكّش).
2. وحدات `h/v` وقيمة `radius` (الـargument، الافتراضي = constRadius 60): مصدرها `ClientSpinController` (UI) ما اتفكّش.
3. الستيك المستخدم (default cueDividor = 0.75 = الأكتر شيوعًا؛ `defaultcue` مش في cueitemsConfig).
4. tip بالظبط في المنتصف ⇒ spinAngleDeg=0 (حالة الـreset)؛ بدون الافتراض ده الـmapping بيدّي −180° (غير مقصود في اللعبة غالبًا). deadZone (7) معناه في ClientSpinController ما اتفحصش.
5. `intBallAngle` لـmapping = زاوية الضربة الصحيحة (angleInt) للكرة، و90 للحيطة (افتراض).
6. هوية الـsingleton = GamePhysics (استنتاج، فوق).
