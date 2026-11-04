# FINDINGS P2Q (2026-10-31) — بند 4: قيم ServerTracker + منطق OnServerSimulate
المصادر: `level5` (من ReMius_ServerTracker_Component_Evidence.zip)، `libil2cpp.so` (ARM64)، `libunity.so` ما اتفتحش في الجزء ده.
ملحوظة حزم: `ReMius_ServerTracker_CoreAssets.zip` فيه `level4`/`level5`/`resources.assets` **صفر بايت** — الـlevel5 الحقيقي من zip الـEvidence.

## 1) القيم المتسلسلة — محسومة (موثّق المصدر)
MonoBehaviour PathID 84 في level5 (script PPtr (1,443) = ServerTracker)، 160 بايت، من غير TypeTree. الـ40 بايت الأخيرة:
`minXpoint -0.0015 · maxXpoint 0.0015 · minZpoint -0.0015 · maxZpoint 0.0015 · minXRack -0.07 · maxXRack 0.07 · substeps 7.0 · stopSimulateThreshold 0.002 · solvecount 8 · isSolving 0`.
**تأكيد التخطيط من الباينري (ده اللي كان ناقص عشان مفيش TypeTree):** الكود بيستخدم +0x140 **float** (`ldr s15`)، +0x144 **float** (`fcmp`)، +0x148 **int32** (`str w8`)، +0x14C **bool** (`ldrb/strb`). الـoffset جوه الـcomponent + 0xB0 = الـoffset المُدار للأربعة (والستة floats اللي قبلهم من 0x128) ⇒ نفس الترتيب والأنواع، مفيش فجوة. القيمة 7.0 كـfloat صح (كـint كانت هتبقى 1088421888). الاستنتاج الوحيد المتبقي: إن ده الـinstance الوحيد (اتفحصت كل المشاهد المرفوعة؛ مفيش غيره).
- `solvePasses = 8` اللي كان "MASTER_8 مش متحقق" ⇒ **اتحقق** (solvecount).
- `stop = 0.002` ⇒ اتحقق. الـpredicate (4 نسخ، 0x5DB79D4…): CueBall موجودة && !InTube && |rb.linearVelocity| (3D، sqrt float) **>** الـthreshold (strict). المحرك بيستخدم sqrt(vx²+vz²) > stop (Y مثبّت) ⇒ متطابق شكلًا؛ الفرق: اللعبة بتفحص **البيضا بس** في الـpredicate ده (مين بيستخدمه لإنهاء التسديدة لسه مش مفكوك — افتراض إن المحرك بيفحص كل الكور).

## 2) OnServerSimulate (0x5DB856C) — اتفك بالكامل
- حي: `FixedUpdate` (0x5DB99F4) بيعمل tail-`b` عليه. **`frq` (0x5DB6674) نسخة طبق الأصل ميتة**: مفيش bl/b ولا مؤشر غير MethodInfo (0x7836560).
- **المسار العادي:** `if (!isSolving) Physics.Simulate(Time.fixedDeltaTime)` **مرة واحدة** (مفيش sub-steps).
- **المسار الخاص** (onSimulate +0x110 && !InTube && CueBall+0x10B==0 && CueBall+0x26C==0): كل FixedUpdate
  `cueRb.linearVelocity = (baseline(+0xD4) + normalize(vec(+0xBC))·|vec|) − cueRb.position` (Rigidbody.set_linearVelocity / get_position، متأكدة من أسماء الـicall).
  - **فلاج +0x108 = true:** `if (substeps<=0) return;` وإلا `do { Physics.Simulate(fixedDelta / substeps) } while (substeps > i++)` ⇒ ceil(substeps) نداء = **7 نداء بـfixedDelta/7 (float32)**.
  - **فلاج false:** لو |target − pos| < stopSimulateThreshold ⇒ `rb.position = target`، `linearVelocity = 0`، `onSimulate = false`؛ بعدين `Simulate(fixedDelta)` مرة.
- **هوية الفلاج +0x108** (كانت "مش ظاهرة في dump"): بيتكتب في 3 نسخ من دالة بداية المحاكاة (0x5DB70C0 / 0x5DB8BF0 / 0x5DB9018 — مفيش نداء مباشر، بتتنادي بـdelegate/MethodInfo) من رجوع `CueBall` method **0x5DD6BB8** = `Physics.SphereCast`(origin = موضع البيضا، radius = `CueBall+0xB4 + 2·Physics.defaultContactOffset`، dir = normalize(vec) (لو |vec|≤1e-5 ⇒ static vector)، maxDistance = |vec|، layerMask = int من singleton+0xF8). يعني: **الفلاج = "المسار هيخبط حاجة؟"** (استنتاج من الـARM64؛ مش مقارن بلعبة). نفس الدالة بتكتب +0x110 = true بعده.

## 3) أثر ده على المحرك
- `Cfg.substepsF=7.0f` (مصدره level5 دلوقتي مش الأداة القديمة)، `substeps=ceil` بنفس حلقة اللعبة، `dt = float32(fixedDelta)/7` (فرق ~1e-10 عن القديم)، `setSubsteps()` + CLI `--substeps`. `solvePasses=8`، `stop=0.002` موثّقين. مسار الـpath بقى يعتمد على `C.substeps` بدل 7 ثابتة.
- **اللي لسه مش مطابق (مهم):** المحرك بيشغّل 7 sub-steps دايمًا. اللعبة بتعمل ده بس في المسار الخاص لما الـSphereCast يلاقي حاجة؛ وغير كده خطوة واحدة 0.01 ث. ومحاكاة المسار الخاص نفسه (velocity = target − pos كل tick) **ما اتعملتش** لأن معنى `vec`/`baseline` ومين بينادي دوال البداية الـ3 مش محسوم (مفيش callers مباشرة). ما حطّيتش افتراض صامت: المحرك على السلوك القديم ده، والفرق مكتوب هنا.
  - يحسمه: `dump.cs` (أسماء الدوال الـ3 + مين بيسجّلها) أو parser لـglobal-metadata.dat v39 (header اتفحص: sanity 0xFAB11BAF، version 39، ما اتفكّش) + تتبّع الـdelegates.
- **ترتيب الـdamping (linearDamping 0.62 جوه الـsub-step):** ده جوه PhysX (libunity) — مش في libil2cpp. **ما اتحسمش**؛ بيتحسم مع بند 7. المحرك لسه `--drag-mode` (افتراض).

## 4) أدوات/ملفات جديدة
`src/pp2_servertracker.hpp` (Tail + parseTail + substepCount + substepDt)، `tests/test_servertracker.cpp` (18 فحص اتساق، مش مقارنة بلعبة)، `data/servertracker_level5{.json,_pathid84.bin}`، `tools/il2cpp/fieldscan.py` (مسح ldr/str لـoffsets مُدارة في مدى)، `tools/il2cpp/st_tail.py` (فك الـtail من أي level).
