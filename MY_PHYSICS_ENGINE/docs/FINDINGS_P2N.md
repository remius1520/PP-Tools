# FINDINGS P2N (2026-10-31) — بند 3: SmoothDamp لزاوية θ (+0x254) والـfallback Vector3

## محسوم (من libil2cpp)
- θ المستخدمة = `CueBall.correctedRotAngel` (+0x254)، سرعتها `refRotAngel` (+0x258). بتتحدّث **مرة كل فريم** في `PredictCueRotationAngelAfterCollision` (0x5DD7490) بـ`Mathf.SmoothDamp(+0x254, target, ref +0x258, 0.03, +inf, Time.deltaTime)` (0x6C16034). النداء من `OnHandleCueDirection` (0x5DD6EE0، بعد ما يحسب `intBallAngle` +0x1C4 مباشرة) وكمان من `imm`/`emx`/`nrv` (نسخ).
- بعدها `OnHandleCueDirection` بتقرا +0x254 ⇒ `Euler(0, θ·0.0174533, 0)` (0x5DD7188–0x5DD71A8) = الـclassic quaternion.
- جدول الأهداف متأكّد من التعليمات (>180→180 · <88→95 · 88–89→150 · 90→180 · 91–92→210 · >92→265)، ومطابق لـ`cueTheta` في المحرك.
- `oas` (0x5DD6D94) و`bxn` (0x5DDB2F4) بيعملوا SmoothDamp على +0x254 بثوابت مش منطقية (target 5735 / smoothTime 870 / −463 / 9061) ولا حد بينادي عليهم ⇒ نسخ obfuscation، **متجاهلين**.
- **الـfallback Vector3 [class+0xB8]** = `Vector3.zero`: نفس الـslot (0x74DB238) اللي بيستخدمه `Vector3.Normalize`/`get_zero` (0x6C1283C / 0x6C132C4)، والمنطق = `Normalize` (kEpsilon 1e-5 ⇒ صفر). وكمان موجود بس جوه `SolveCollisions` اللي بقت كود ميت (P2M) ⇒ مفيش أثر على المحاكي.

## تقدير زمن التقارب (حساب من صيغة Unity، مش من اللعبة)
بافتراض `Time.deltaTime = realDt × timeScale(1.36)`: θ بيوصل لأقل من 0.01° من الهدف بعد ~0.12–0.17 ثانية من ثبات الـbucket (قفزة 30–170°؛ 30–120 fps). يعني الافتراض "وصلت للهدف" سليم لو الهدف ثابت قبل الضربة بحوالي ٠.٢ ثانية؛ غير كده فيه فرق.

## الكود
- `Shot.thetaStartDeg` (<0 = converged، الافتراضي) + `thetaFrames` (0 = converged) + `thetaDt`: لو اتحددوا، المحرك بيشغّل `thetaFrames` خطوة SmoothDamp (float زي اللعبة، سرعة بداية 0) من `thetaStartDeg` ناحية قيمة الجدول. CLI: `thetahold <startDeg> <frames> [dt]`. الافتراضي (من غيره) = السلوك القديم بالظبط.
- اختبار 13 في `test_engine.cpp`: 0 فريم = بدون تغيير؛ خطوة واحدة = نفس `postcol::smoothDamp`؛ 40 فريم = وصل للجدول. (اختبار اتساق، مش مقارنة بلعبة.)

## افتراضات (مش محسومة)
1. سرعة البداية `refRotAngel` = 0 (القيمة الحقيقية runtime من آخر فريم).
2. `Time.deltaTime` = realDt × 1.36 (من timeScale؛ لم يتأكد إن deltaTime في اللعبة متأثر بيه بالشكل ده).
3. θ و+0x258 مبيتصفّروش بين الضربات (مفيش ResetBall/ResetCollision فحصناهم لكده): `ResetBall` 0x5DDABE8 مش مفحوص لحقول +0x254/+0x258.
