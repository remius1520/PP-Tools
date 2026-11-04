# FINDINGS P2X — Y الكورة لكل الترابيزات + فيزياء الترابيزات + hull الجيب 3D + SmoothDamp
## 1) Y الكورة (اتقفل لكل الترابيزات الست)
- `GameTableSetup.InitializeTableBalls` (0x5D62330): بيقرأ `Transform.get_localPosition` لكل Rock1 تحت `TableSettingsBase.RockTransforms (+0x28)` وبيمرّره pos لـ`OnSpawnBall` (0x5D62EBC): Instantiate(pos,rot) + `localScale=ballScale` + Spawn. مفيش تعديل Y.
- يعني Y الكورة = **local Y** للـRock (مش world). Level3: root عند y=2.523 فالـworld 2.5065 كان مضلل ⇒ local = **-0.0165**. Level13: root y=0.032 ⇒ local = **-0.0275**. باقي الترابيزات root عند 0 (5: -0.0235 · 7: -0.0239 · 9/11: -0.0296) = نفس القيم القديمة.
- المحرك: `kTables` كلها `ySourced=true`. Side pockets لسه بتدخل في الست ترابيزات (`test_tables` 24/24).
## 2) فيزياء الترابيزات (اتقفل)
- Rigidbody الكورة من prefab واحد مشترك (BallBase / Solo(1): mass 10 · drag 0.62 · angDrag 1، sharedassets0)، وGamePhysics واحد في level5 بنفس القيم. TableSettingsBase مفيهوش mass/drag.
- الـ5 PhysicMaterials (Ball_Physics, EdgePhysics_Snooker, Edges_Physics, Pocket_Physics) bounce 0.92 واحتكاك 0؛ Table_Physics bounce 0 واحتكاك 0 (frictionCombine=3). bounceCombine=2 (Minimum، P2P). كل ترابيزة بتستخدم نفس المواد ⇒ قيم Level 11 صحيحة لكل الترابيزات.
## 3) قاعدة دخول الجيب = sphere vs 3D convex hull (اتنفّذ)
- كل الـ36 trigger meshes: convex=1، 136 vertex ⇒ hull 32 vertex / 60 مثلث (scipy ConvexHull، `tools/emit_hull3d.py` ⇒ `data/<key>_pocket_hull3d.txt`، + قسم `HUL3` في `pack_data.py`/`loadBinary`).
- المحرك: `Cfg.pocketHull3d=true` (`--pocket-prism` للقديم): دخول = أول t فيه distance(مركز، hull) ≤ R. **من معرفة عامة بـPhysX** (GJK overlap، من غير contactOffset للـtriggers) — مش من فك libunity.
- الفرق عن prism: ≤0.007 مم في 1_4/7_8/11/13، 0.25 مم lvl9، **4.95 مم lvl5_6**. `tests/test_hull3d.cpp` 789 فحص (اتساق، مش مقارنة باللعبة).
## 4) SmoothDamp θ (CueBall)
- `+0x258` (velocity) بيتبعت ref للـSmoothDamp في 3 أماكن بس (0x5DD6DF8, 0x5DD7538, 0x5DDB370)؛ `+0x254` بيتكتب من ناتج SmoothDamp بس (0x5DD6E08, 0x5DD7548, 0x5DDB380). **مفيش reset صريح** في CueBall ⇒ الحالة بتفضل بين الضربات. الـdt = `Time.deltaTime`.
## لسه مفتوح
- بند 7 PhysX الحقيقي (مقصود). هوية field `ServerTracker+0x3E0` لسه مش محسومة. أثر `ResetSpinValues`، الستيك الافتراضي، قيمة بداية الـvelocity عند أول فتح. مفيش مقارنة بضربة مرجعية ⇒ "موثّق المصدر" مش "مطابق".
