# PP2 — HANDOFF P2A (قراءة ملفات Unity: فيزياء + ترابيزات، 2026-10-30)

`sh tests/run_all.sh` ناجح (P0 34 · P1 25 · P1B 27 · صيغ 19 · أول تلامس 29 · **هندسة ترابيزات 28 جديد**). لسه ولا حاجة اتجرّبت على جهاز.

## أدوات القراءة (كتبتها من الصفر، بدون UnityPy)
| أداة | شغلها |
|---|---|
| `tools/ufs.py` | فك UnityFS (LZ4/LZMA): `python3 tools/ufs.py <bundle> <outdir>` |
| `tools/sf.py` | قارئ SerializedFile v22 (Unity 6000.3.9f1)، مع type tree لو موجود |
| `tools/sa0.py` + `export_tables.py` | قراءة `sharedassets0.assets` **من غير type tree** (layouts متحقّقة بأحجام الـobjects) |
| `tools/gen_tablegeo.py` | يولّد `core/sim/pp2_tablegeo_data.hpp` + `data/pp2_tables*.json` |
| `tools/freespace.py` | خريطة المساحة الحرة للكورة (مسافة 3D لأقرب مثلث) عند ارتفاع الكورة |

الترتيب: `PP2_WORK=<dir>`؛ فك `assets/bin/Data/data.unity3d` في `<dir>/dat` (فيه `globalgamemanagers` و`sharedassets0.assets` و`.resS`) ثم `python3 tools/gen_tablegeo.py <outdir>`.
**`sharedassets0.assets` كان جوه `data.unity3d` طول الوقت — مش محتاج ملف منفصل.**

## متأكد من الملفات (مصدر: `globalgamemanagers` + `sharedassets0.assets`)
- **TimeManager**: fixedDelta = 1411199/141120000 s (= 0.01f)، timeScale **1.36**، maxAllowedTimestep 0.3333، maxParticleTimestep 0.09.
- **PhysicsManager**: gravity (0,−9,0)، **bounceThreshold 0** (اتأكد)، defaultMaxDepenetrationVelocity 10، sleepThreshold 0.001، **defaultContactOffset 0.001**، solver 30 / velocity 20، مصفوفة الطبقات كلها تتصادم، defaultMaterial = null.
- **Rigidbody الكورة**: mass 10، drag 0.62، angularDrag 1.0، useGravity، **constraints = 4 (FreezePositionY)**، interpolate، collisionDetection = 2 (ContinuousDynamic). ترتيب آخر 3 حقول مستنتج من الـlayout.
- **SphereCollider**: radius 0.5 (× scale)، مادة `Ball_Physics`، طبقة Ball (10).
- **PhysicMaterials**: Ball/EdgePhysics_Snooker/Edges/Pocket: friction 0/0، **bounciness 0.92**، combine الاحتكاك=2 والارتداد=2. `Table_Physics`: 0/0/0، combine احتكاك 3. كولايدرز `Corners-Table` و`Poket-Down` **من غير مادة** (PhysX الافتراضية).
- **التاجات**: 20000 Edge · 20001 Ball · 20002 PocketInside · 20003 Table · 20004 Cutout · 20005 Black · 20006 Scroll · 20007 Cuebase · 20008 CircleFoulCenter. **الطبقات**: 3 AimCast · 8 Cue · 10 Ball · 11 Edge · 15 Level.
- **افتراض مش محسوم**: ترتيب enum الـcombine (Average 0 · Multiply 1 · Minimum 2 · Maximum 3) من معرفتي بـUnity؛ الـenum مش في dump.cs ولا الـmetadata. لو صح: كورة/حيطة = min(0.92,0.92) = **0.92**، وكورة/ترابيزة = 0 (مفيش ارتداد رأسي). لو كان Multiply هتبقى 0.846.

## الترابيزات (6 في المشهد)
`Lvl1_4` · `Lvl5_6` · `Lvl7_8` · `Lvl9` · `Lvl9_12` · `Lvl13`. أبعاد الوش الداخلي للحيطان (إحداثيات المشهد) والبيانات الكاملة في `data/pp2_tables_summary.json`. مثال `Lvl9_12`: x ±1.1048، z من −0.7307 لـ 0.3647 (2.2095 × 1.0953). نصف قطر الكورة = 0.5 × scale صفوف الـRocks المقابلة: Lvl1_4 0.040 · 5_6 0.0325 · 7_8 0.0335 · 9 / 9_12 0.0275 · 13 0.0235 (**scale المشهد؛ الـruntime ممكن يغيّره**، والمطابقة rock↔ترابيزة بالاسم والقرب).
- كل ترابيزة: 6 كولايدرز `Edge` (mesh غير محدّب، non-trigger، مادة Edges_Physics)، 6 triggers `PocketInside` (طبقة AimCast)، 6 triggers `Cutout`، و6 كولايدرز `PocketInside` **صلبة** (مادة Pocket_Physics 0.92).
- **الـEdge في Lvl1_4 صناديق (12 مثلث)؛ في Lvl9_12 وغيرها 45 vertex / 32 مثلث بأطراف مشطوفة عند الجيب** — مش صناديق بسيطة.
- **شكل الجيب الحقيقي** بيتحدّد بـ`PocketInside` الصلبة (530 vertex) + `Corners-Table` (1084 vertex) مش بنهايات الـEdge. فتحة الركن بالـbbox (~0.048م) أضيق من الفعلية (~0.064م في Lvl9_12 من خريطة المساحة الحرة).
- مفيش ترابيزة بتتفعّل من الملفات لوحدها: أي واحدة شغالة في الماتش = حقيقة runtime. `geo::nearest(xl,xr,zb,zt)` بتختار الترابيزة من أوجه حيطان مقاسة.

## المحاكي
- `core/sim/pp2_tablegeo.hpp`: `geo::tables()`، `find(key)`، `nearest()`، `toSimTable(geo, r, shrink)` — حدود مركز الكورة = وش الحيطة ∓ r **دقيقة من الملفات**. فتحات الجيوب تقريبية (من نهايات الـEdge) ومعلّمة كده.
- `sim::Pocket` فيه `gapAlongX/gapAlongZ` (الركن ليه فتحتين مختلفتين). الافتراضي −1 = زي قبل، فمفيش اختبار قديم اتأثر.
- `Params::bounceThreshold = 0` بقى مؤكد من الملف.

## الناقص بالترتيب
1. **P2B: backend تصادم mesh** (كورة ضد مثلثات بـCCD، y مثبّت): البيانات جاهزة في `data/pp2_tables.json` (مثلثات world-space لكل الكولايدرز الصلبة). ده اللي هيدّي دقة الجيوب والبانك قرب الجيوب. جرّبت فتحات 1-D من الـmesh (`tools/mouths.py`) وطلعت **غير موثوقة** (حساسة لشكل الـjaw) فما اتربطتش بالمحاكي.
2. **`timeScale 1.36`**: هل بيأثر على `Physics.Simulate`؟ (فك Update/ServerTracker لسه).
3. بنود فك `libil2cpp.so` من الملف الأم (3 حلّال التداخل، 4 السبين، 5 الباور→F، 7 SmoothDamp، 8 التاج المستثنى) **ما اتلمستش في الجلسة دي**.
4. **اتجاه `u`** (بند 6): `cuegroup_*.bundle` ليه type tree بس جدول الـcommon strings لـ6000.3 ناقص في `sf.py` (`COMMON`) فالقارئ وقع. محتاج جدول الأسماء الصح أو قراءة أسماء الحقول من نص الـbundle.
5. تجربة الجهاز (كل الافتراضات في `HANDOFF_P1D.md` لسه زي ما هي).
