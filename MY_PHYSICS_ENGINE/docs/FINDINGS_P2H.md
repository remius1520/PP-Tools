# FINDINGS P2H — كاتبو CueBall+0x220 / +0x22C (فك ARM64 مستقل، 2026-10-31)
المصدر: `libil2cpp.so` (part01+02) + `dump.cs` + `global-metadata.dat` فقط. مفيش تسجيل ولا fit. الكود: `src/pp2_clientpredict.hpp` + `tests/test_clientpredict.cpp` (18 فحص). الفحوص دي اتساق داخلي مش مقارنة بضربة حقيقية.

## اتحسم
**`collidedBallForceDirection` (+0x22C)** و`targetBallDirectionRay` (+0x238): بيكتبهم `CueBall.OnCastBall` (0x5DD7FDC؛ نسخة مشوّشة `md` 0x5DDAFC4) من `PhysicsBuilder.PredictTargetBallAfterCollision(hit.point, ballRadius(+0xB4), hit.normal)` (0x5D3CC58):
- `d = normalize_xz(n)` (صفر لو |n_xz| ≤ 1e-5)؛ Ray.origin = `(hit.x − d.x·r, 0, hit.z − d.z·r)`؛ Ray.direction = `(−d.x, 0, −d.z)` → **الاتجاه = −normalize_xz(hit.normal)**. مطابق لافتراض V5.

**`forceDirectionAfterCollided` (+0x220)**: بيكتبه `CueBall.OnHandleCueDirection` (0x5DD6EE0؛ نسخ مشوّشة imm/emx/nrv):
1. `u = stick.transform.up`، `Rn = Euler(0,−π/2,0)·hit.normal` (الثابت −1.5707964 من rodata 0x131DCA8).
2. `c = −(u·Rn)/√(|u|²|Rn|²)` (لو الجذر < 1e-15 بيسيب |u|² مكان الزاوية)، clamp[-1,1]، `ballAngle = acos(c)·57.2958` → +0x1BC، و`intBallAngle` (+0x1C4) = RoundToInt.
3. `PredictCueRotationAngelAfterCollision` (0x5DD7490) بيحدّث +0x254 بـSmoothDamp؛ `classic = Euler(0, +0x254·0.0174533, 0)`، `spin = Euler(0, SpinAngelDeg·0.0174533, 0)`.
4. collider.tag == "Ball" (`a.nn()` = "Ball"، اتفك من الـblob) ⇒ `PredictCueDirAfterBallCollision(classic, spin, n)`، غير كده ⇒ `PredictCueDirAfterEdgeCollision(spin, CueBall+0x1B0 (scaledDirection), n)`.
5. الناتج بيتطبّع **ثلاثي الأبعاد** (لو الطول > 1e-5، وإلا القيمة القديمة في الريجستر) ويتخزّن في +0x220 (ونسخة تانية في +0x200).

**الدالتين:**
- Ball (0x5D420A8): `q = (Euler(spin).y بعد MakePositive ≠ 0) ? spin : classic`، الناتج `q·n`. يعني **من غير سبين (SpinAngelDeg = 0) = جدول θ، ولو في سبين الزاوية بتاعته بتحل محل الجدول** — مطابق لجملة V5 "with spin the spin quaternion replaces it".
- Edge (0x5D43858): `d = v·n` (الـ3 مكوّنات)، `w = (v.x−2d·n.x, 0, v.z−2d·n.z)`، الناتج `spin·w` — **السبين بيتطبّق دايماً هنا** (مع صفر = identity).

**SpinAngelDeg** = `NetworkVariable<float> spinAngelDeg` (+0xC8) بتتكتب من الحقل `angleDeg` (+0xC0) عن طريق `OnUpdateSpinAngel` (0x5DA110C). `OnResetSpin`/`ResetSpinValues` بيصفّروها (0). `GetSpinAngelMapping` (0x5DA186C) بتكتبها = `Round(atan2(...)·57.2958 + 180 مطوي لـ(−180,180], 1 خانة)`، والصفر ⇒ 0.1.

## اللي اتغيّر في فهمي
لا شيء بيناقض V5 في الاتجاهات. الجديد: (أ) الـfallback والتطبيع 3D (ب) Edge بيطبّق السبين دايماً (ج) الشرط الدقيق لاستبدال الجدول بزاوية السبين.

## غير محسوم / لسه
1. **enum دمج الارتداد (مهم):** `GAME_FILE_FACTS.md` بيقول Average0/Minimum1/**Multiply2**/Maximum3 وكل الـmaterials قيمة الدمج 2، بينما CHANGES_v3 افترض Minimum=2 (⇒0.92). ترتيب PhysX الأصلي (`PxCombineMode`) من معرفتي العامة هو Average,Min,Multiply,Max يعني 2 = Multiply ⇒ 0.92×0.92 = **0.8464** لأي تصادم PhysX مش متغطّى بسكربت اللعبة (مثلاً البيضا على الحيطة من غير سبين). **ماتأكدش من libunity.so** وما اتغيّرش في المحرك. لازم يتحسم قبل أي كلام عن دقة الباند.
2. SmoothDamp لزاوية θ وقت الضربة (+0x254): بيتحدّث كل فريم، المحاكي بيفترض إنه وصل للهدف. محتاج توقيت الفريم (مش في الملفات).
3. `Euler(spin).y == 0` عند 180° بالظبط (ToEulerRad في libunity) — بتتعامل هنا كأن الشرط = `SpinAngelDeg != 0`.
4. fixedDeltaTime وترتيب الـdamping: محتاجين `data.unity3d` (TimeManager) + libunity؛ ما اتبدأش.
5. **اتربط جزئيًا:** `Sim::predict` بقى بياخد `Shot.spinAngleDeg` (افتراضي 0 = السلوك القديم بالظبط؛ CLI: `spinangle <deg>`) ويطبّق شرط الاستبدال في الكورة وSpin·reflect في الحيطة. باقي: ربط `forceDirectionAfterCollided` الـ3D والـfallback، و`SpinFactor` نفسه من موضع الـtip (pp2_spin.hpp) بالمحاكي.
