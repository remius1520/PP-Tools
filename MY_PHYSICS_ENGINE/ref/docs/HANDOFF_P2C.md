# PP2 — HANDOFF P2C (توصيل mesh بالـpayload، 2026-10-30)
`sh tests/run_all.sh` ناجح (38 اختبار جديد في `tests/test_scene.cpp`). **لسه ولا حاجة اتجرّبت على جهاز.**

## اللي اتعمل
- `core/sim/pp2_scene.hpp`: `selectTable()` يختار الترابيزة من (نصف قطر الكورة runtime، مواضع الجيوب، حدود SphereCast اختياري). **مش بيخمّن**: لو ترابيزتين بنفس الدقة (Lvl9 / Lvl9_12 نفس القطر) ومفيش calibration → `ambiguous` والـmesh بيترفض والـlegacy يكمّل. `Scene::build()` يبني `MeshWorld` + `Table` (obs موصّل) ويرفض لو ارتفاع الكورة غير منطقي أو مفيش مثلثات.
- الـpayload: `meshTick()` (في `recFrame`) يبني المشهد مرة لكل ماتش، ويعيد البناء لما calibration يوصل. cfg جديد `mesh` (افتراضي 1). الحالة في `pp2_mesh.txt` (اتضاف لـreport.sh وinstall.sh).
- فك `UpdateHorizontalForce` (0x5D40C90): setter بس، `str s0,[x0,#0x20]` يعني الحقل `horizontalForce` @0x20 في PhysicsBuilder. الـoverride الفعلي لسه ما اتفكّش.
- أدوات الفك: `tools/a64dis.py` بيشتغل بعد تجهيز `/home/claude/rvamap.pkl` + `rd.py` + `so/libil2cpp.so` (لصق part01+part02)؛ مفيش llvm-objdump/capstone في البيئة.

## لسه (بالترتيب)
1. فك: الباور→F (override)، السبين (`ApplySpinFactorCombination`, `CalcMaxSpinValues`), `SolveBalls`, اتجاه `u`, SmoothDamp.
2. محاكاة السبين → P3 رسم يستخدم `Scene` بدل المستطيل → P4 العقل → P5 واجهة → P6 تثبيت.
3. **تجربة جهاز + build أندرويد** (ما اتعملش أبداً).

## P2D (نفس الجلسة): فك الباور → F واتجاه u (من libil2cpp)
- `GetStickForwardCastDireciton` (0x5D3DAC8): `d = stick.transform.up / |up|₃ᴅ`، `forceDirection(+0x28) = (d.x·F, 0, d.z·F)`، `F = horizontalForce (+0x20)`. التطبيع بالطول ثلاثي الأبعاد → ستيك مرفوع يقلّل |forceDirection| (= F·cos(elev)). لو |up| ≤ 1e-5 بيستخدم اتجاه كائن بديل. اتنفّذت في `game::forceDirectionFromStickUp` + 3 اختبارات.
- **`u = stick.transform.up` هو اتجاه الضربة نفسه** (forceDirection بيشاور على +up) → افتراض P1C اتأكد.
- `GetForceDirectionToHit` (0x5D3DD60): لو الكائن عند cueBall+0x218 غير حي، `forceDirectionForCollide(+0x34) = forceDirection`.
- **F مش محسوب في كود C#**: setters `horizontalForce` (UpdateHorizontalForce 0x5D40C90، set_HorizontalForce 0x5D4330C، lqv 0x5D414FC) ملهاش ولا `bl` مباشر في libil2cpp → بتتنادى من UnityEvent/سلايدر في الـscene. **الحل: الـpayload يقرا `PhysicsBuilder.horizontalForce` و`forceDirection` live** (الحقول `pb.hforce` متسجّلة أصلاً). مفيش صيغة محتاجة.
- `levelMaxPower` (+0x48) بيتكتب من `UpdateLevelPower` اللي بتتنادى من `ServerTracker.SyncFirstShotUpdatesRPC` (RPC بيبعت val + arrowval) → قيمة وليست صيغة.
- أداة جديدة: ماسح مستدعين bl/b بـnumpy (كان `callers.py` في /home/claude، مش في الـzip).

## الجزء A — جلسة 1 (السبين)
- `core/sim/pp2_spin.hpp` + `tests/test_spin.cpp` (10 فحوص): `divideTheCircle` و`applySpinFactorCombination` منقولين 1:1 من ARM64.
- **حسم:** `f` = `GamePhysics.Instance.physicsBuilder.finalVelocity` (GamePhysics.physicsBuilder @0x20، PhysicsBuilder +0x40). ومن غير سبين (h=v=0) النتيجة = f (بشرط f ≤ max) → افتراض "SpinFactor = finalVelocity" **اتأكد من الكود**.
- المدخلات كلها حقول live من SpinController (الأوفسيتات في رأس الملف) → الـpayload يقراها؛ مفيش fit.
- **لسه:** `CalcMaxSpinValues` (بتكتب الـmax/increments) فكها 40%، `SyncSpin/ApplySpin` (تأثير السبين على السرعة والاتجاه)، قراءة الحقول في الـpayload، ربط المحاكي.
