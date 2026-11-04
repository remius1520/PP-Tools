# FINDINGS P2Y (2026-10-31) — بند 7 (PhysX الحقيقي): بداية الفك من libunity.so — **البند لسه مفتوح**
> الحالة: اتفتح البند واتحسمت 4 حاجات من الباينري (تحت). **مفيش تغيير في كود المحرك** (الـ12 اختبار نفس النتايج). ما اتقفلش: صيغة الـdamping جوه الـintegrate، الـsolver (30/20)، الـspeculative contacts، CCD. مفيش مقارنة بضربة مرجعية ⇒ "موثّق المصدر" مش "مطابق".

## 1) إصدار PhysX = 4.1.2 (من الباينري)
- `libunity.so` = Unity **6000.3.9f1** (string). الدالة اللي بتطبع "Wrong version: PhysX version is 0x%08x" (xref عند 0x1374194): قبلها `movz w9,#0x200 ; movk w9,#0x401,lsl #16 ; cmp w0,w9` عند 0x137404c..0x1374058 ⇒ **PX_PHYSICS_VERSION = 0x04010200 = 4.1.2**.
- strings: `PxsCCD.cpp`، `ScScene.updateCCDMultiPass/SinglePass`، `PxsContext.CCDSweep/postCCDDepenetrate` ⇒ مسار CCD متعدد التمريرات موجود (الكورة ContinuousDynamic ⇒ بتدخل فيه). `m_FrictionType`, `m_EnableAdaptiveForce`, `m_DefaultSolverIterations/VelocityIterations`, `m_DefaultMaxDepenetrationVelocity` موجودين كإعدادات.

## 2) سلسلة الـdamping من Unity لحد حقل الـcore (متتبّعة بالكامل)
`UnityEngine.Rigidbody::set_linearDamping_Injected` (string 0x187694، تسجيل عند 0x56cd10 → stub 0x56c908 → **0x792c08**):
1. 0x792c08 = `Rigidbody::SetLinearDamping`: clamp `[0, FLT_MAX]` (fmaxnm 0 / fminnm 0x7f7fffff)، بيكتب في كائن Unity عند **+0xA4** (linear) وبعده +0xA8 (angular)، ولو اتغيّر بيبعت أمر **0x0C000005** مع payload 8 بايت = {linear, angular} (0x792c68..0x792c88).
2. الأمر 0x0C000005 ⇒ type = ((0x0C000005>>26)−1)&0x3f = **2**، index 5 ⇒ جدول type-2 (builder 0xc05924، جدول .bss 0x176ba50) ⇒ handler **0xc04854**: `vtable+0x118(float[0])` ثم `vtable+0x128(float[1])` (= setLinearDamping ثم setAngularDamping).
3. vtable الـNpRigidDynamic الحقيقي = **0x1660e78** (91 slot، من .rela.dyn): slot 0x118 → `NpRigidDynamic::setLinearDamping` (0x13764c8)، slot 0x128 → `setAngularDamping` (0x1376580). بيتحول لـScb: لو مش buffered ⇒ ينده `Sc::BodyCore::setLinearDamping` (0x13e0388) / `setAngularDamping` (0x13e0410)، وإلا يكتب في الـbuffer (+0xB4).
4. **Sc::BodyCore: linearDamping عند +0x78، angularDamping عند +0x7C** (str s0,[x0,#0x78] / [x0,#0x7c]؛ الـgetters 0x13e0360 / 0x13e03e8). الـbuffer: +0x30 / +0x34. كائن الـcore جوه NpRigidDynamic عند +0x80.
- يعني قيمة الـ0.62 بتوصل PhysX من غير أي تحويل (clamp ≥0 بس) — **اتأكد من الباينري**.

## 3) خريطة أوامر الـPhysX backend (دالة التوزيع 0xc02f30)
6 جداول type (بتتبني في 0xc031e4): t0 (0x176b908، 16 أمر: Material) · t1 (0x176b990، 23: Scene/World) · t2 (0x176ba50، 49: RigidDynamic-API) · t3 (0x176bbe0، 30) · t4 (0x176bce8، 13) · t5 (0x176bd58، 29) · t6 (0x176be48، 93). الجداول كلها مستخرجة بـ`tools/libunity/p2y_tables.py` (idx→handler). `p2y_handlers.py` بيطبع لكل handler offsets الـvtable اللي بينديها. هوية t3/t4/t5 ما اتحسمتش.
- t2 (من الـhandlers): idx 18 = setLinearVelocity (vtable+0x140، vec3+bool)، 19 = getLinearVelocity (+0x138)، 20/21 = Angular (+0x150/+0x148)، 22/23 = setter/getter float (+0x168/+0x170)، 24/25 (+0x158/+0x160). (هوية الأسماء من ترتيب الـAPI العام، مش مؤكدة بالاسم.)

## 4) محاولات الوصول لدالة الـintegrate (ما نجحتش لسه)
- أداة `p2y_vdis.py` (مفكك NEON: fmul/fadd/fsub/fmax/fmin/fmla/by-element/ldr q) بدل udis اللي كان بيرجّع `.word`.
- المسح عن نمط `fmul → fsub(1,·) → fmax(·,0)` (scalar: `p2y_scan3.py`، vector: `p2y_scan4.py`) رجّع مرشحين (0xbdad8c، 0xbf0acc، 0xbfaae0 = قيود مفاصل/drive؛ 0x14498ec = contact solver clamp؛ 0x145f4b8 = كتلة solver بتقرا +0x70/+0x74 من هيكل constraint). **ولا واحد منهم integrate**.
- لاحظ: `scalar fmsub` في الباينري كله 4 تعليمات بس (0x1207e5c/0x1207e60/0x120831c/0x1208320 = حسابات norm)، يعني الـintegrate مكتوب Vec3V/vector.
- الخطوة الجاية المقترحة: نبدأ من الـcore: دوال بتقرا `BodyCore+0x78` أو (PxsBodyCore +0x70 لو mCore عند +0x08) **مع** `+0x7C` بجانبه، + `maxLinearVelocitySq`؛ أو من `PxScene::simulate` (string `fetchResultsStart`) ⇒ Sc::Scene::stepSetupSolve ⇒ Dy::solverCoreSetup.

## 5) اللي لسه "من معرفة عامة" (ما اتأكدش من الباينري)
- الـintegrate في PhysX 4.1 (`bodyCoreComputeUnconstrainedVelocity`): `v += g·dt` ثم `v *= max(0, 1 − damping·dt)` (linear و angular) قبل الـsolver. ده اللي المحرك بيعمله (`dragMode=0`). **مش محسوم من الباينري**.
- speculative contacts + restitution + bounceThreshold=0، solver 30/20 (PGS)، CCD multi-pass: لسه تقليد تحليلي في المحرك.
