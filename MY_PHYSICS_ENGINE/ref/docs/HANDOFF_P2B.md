# PP2 — HANDOFF P2B (backend تصادم mesh، 2026-10-30)

`sh tests/run_all.sh` + `tests/test_mesh.cpp` (75 فحص). لسه ولا حاجة اتجرّبت على جهاز، والـpayload مش متوصّل بالـbackend.

## الملفات
- `core/sim/pp2_mesh.hpp`: `MeshWorld` (يطبّق `sim::Obstacle`). `build(geo::meshSource(key), *geo::find(key), yc, r)`؛ ثم `Table::obs = &world` في `sim::run`.
- `core/sim/pp2_meshdata.hpp` (مولّد: `tools/gen_meshdata.py`): مثلثات كل الكولايدرز الصلبة + مجسّمات triggers الجيوب. `data/pp2_tables.json` اتجدّد بـ`tools/gen_tablegeo.py` (بيحفظ `triggers`).
- `sim::Obstacle`/`Contact` في `pp2_sim.hpp`؛ لما `Table::obs` موجود بيستبدل حدود المستطيل وفتحات/دوائر الجيوب.

## الفيزياء المطبّقة
- الكورة نقطة في مستوى y=yc، والتصادم sweep تحليلي ضد (منشور الوجه ∪ أسطوانات الحروف ∪ كرات الرؤوس) بنصف قطر r. اتقارن بقياس مسافة مستقل (Ericson) على 5 ترابيزات: أقصى فرق 0.05 مم، 0 اختلافات.
- الرد: انعكاس حول الـnormal المسقط على xz بارتداد e (لأن Y مثبّت: الكتلة الفعّالة |n_xz|²/m)، احتكاك 0. e: Edges/Pocket_Physics 0.92، من غير مادة (Corners-Table, Poket-Down) 0. **افتراض لسه مش متأكد:** ترتيب enum الـcombine (Minimum=2).
- أول تلامس مع تاج Edge بيطبّق override اللعبة؛ الـnormal = normal المثلث.
- **التقاط الجيب (من فك libil2cpp):** `OnTriggerEnter` -> `Ball.OnTriggerOccur` (0x5DD38C8) -> `TryGetComponent<Icollision>` -> `OnCollisionOccuer(ball)`. القاعدة: SphereCollider الكورة يتداخل مع trigger `PocketInside` (محدّب). اتنفّذت بالظبط (داخل الحجم أو مسافة ≤ r + sweep). الـ`Cutout` triggers تحت الترابيزة (y≈-0.35) مش بتلتقط على ارتفاع الكورة.

## مهم
- **yc** (ارتفاع مركز الكورة): للـ5 ترابيزات = `rackY` من الملفات (Lvl5_6 -0.0235، Lvl7_8 -0.0239، Lvl9/9_12 -0.0296، Lvl13 0.0045). **Lvl1_4 غير معروف من الملفات** (الـrack عند y≈2.5): الـpayload يقراه من `ball.transform.position.y` وقت التشغيل. يفضّل يقراه للكل ويقارنه.
- r = 0.5 × scale وقت التشغيل (قد يختلف عن scale المشهد): `build()` لازم يتعاد لو اتغيّر.
- `MeshWorld` مش thread-safe (stamp mutable).
- المحاكاة بتتعامل مع contactOffset=0 (اللعبة 0.001).

## الناقص
1. توصيل `MeshWorld` بالـpayload (اختيار الترابيزة بـ`geo::nearest` + قراءة yc/r).
2. تأكيد enum الـcombine وقيمة الـ0 للأجسام من غير مادة من جهاز (بانك قرب الجيب).
3. بنود فك libil2cpp: حلّال التداخل، السبين، الباور→F، SmoothDamp، timeScale 1.36، اتجاه `u` للستيك.
