# FINDINGS P2W — باقي الترابيزات + قفل الحزمة
## اللي اتعمل (بند 6 اتقفل لهندسة الترابيزات)
- `tools/gen_tables.py` + `tools/emit.py` بيولّدوا من `data.unity3d` (sharedassets0) ملفات `<table>_solids/_pockets/_pocket_hulls.txt` لـ: lvl1_4 · lvl5_6 · lvl7_8 · lvl9 · level11 · lvl13 (موجودين في `data/`).
- **القاعدة اتتحقق على Level 11**: الكوليدر بيدخل لو (enabled) **وكل الـGameObjects اللي فوقه active** (isActive)، مش trigger. النتيجة = نفس الـ8348 مثلث بتاعة الملف القديم واحد-لواحد (أقصى فرق مركز 4.8e-7). ده فسّر ليه `bumperCol-Up*` و`floor-Table` كانوا مستبعدين: GameObjects غير active في الـdata.
- Level 13: الـ4 `Border` (BoxCollider، material 304) اتحولوا لـ12 مثلث لكل واحد؛ `Edge` (mat 303) غير active فمستبعدين.
- الـconvex flag (lvl5_6 Edge): اتفحص إنهم محدّبين فعلاً ⇒ مثلثاتهم = الـhull.
- المحرك: `Sim::loadTable(dir,key)` + `kTables` (R = ballScale/2) · CLI `--table <key>` · API `pp2_load_table`. من غير `--table` = level11 زي ما كان.
- `tests/test_tables.cpp`: لكل ترابيزة: 6 جيوب، عدد الـrails (6 · lvl5_6=8 · lvl13=10)، الجيبين الجانبيين بيدخلوا من مركز الترابيزة، وضربة في حيطة ترتد جوه. 24/24.
## افتراضات صريحة (مش محسومة)
- **Y الكورة** لـ`lvl1_4` و`lvl13` **افتراض** (rackY من الـprefab خارج مدى الـpocket trigger؛ الكورة نفسها مش في أي level scene هنا — runtime prefab/bundle). المحرك بيطبع تحذير. حساسية Lvl1-4: Y ≤ -0.02 ⇒ الجيب الجانبي بيتسد بهندسة الـjaw؛ Y ≥ -0.01 بيدخل. الحسم: `tablegroup_*.bundle`/prefab الكورة.
- فيزياء الـRigidbody (mass/damping/ارتداد) لباقي الترابيزات = قيم Level 11 (مش متفحصة).
- مثلثات الـcorner diagonals مش معيار (القطر من المركز بيخبط الـjaw في كل الترابيزات حتى Level 11).
## لسه مفتوح
- بند 7: PhysX الحقيقي (تقليد). قاعدة دخول الجيب sphere-vs-convex (+R ممكن). مفيش مقارنة بضربة مرجعية ⇒ "موثّق المصدر" مش "مطابق". بند 8 (رسم/واجهة/أندرويد) عند المستخدم.

## تحديث: dumps من ذاكرة اللعبة (Table.zip → `ref/runtime_dumps/`)
قراءة runtime (مش من الملفات) — مصدر مستقل:
- **ballScale (TableSettingsBase+0x38)**: table5 = 0.065 · table7 = 0.067 · table9 = 0.055 · table11 = 0.055 ⇒ يطابق R في `kTables` (R = ballScale/2) للأربعة. اتأكد.
- **GamePhysics (+0x30/34/38)**: mass 10 · drag 0.62 · angularDrag 1 في table5 وtable11 (نفس القيم) ⇒ فيزياء الكورة مش بتتغير بين الترابيزتين دول. **PhysicsBuilder**: levelMaxPower 4.3 و0.04 في 5/9/11. (7 مش متاح: الـbuilder مش live.)
- **Y الكورة**: `table_dump.txt` (16 كورة، R 0.0275) و`table_dump--table 11.txt` ⇒ Y = −0.0296 (كور على الترابيزة) ⇒ يطابق preset لـlevel11/lvl9. Y لـtable5/7 مش في الـdumps (فاضل من rackY).
- **جيوب Level 11**: `pocketObj` runtime = مراكز الـCheckBall triggers اللي استخرجتها من data.unity3d بفرق ≤ 0.0001 على الستة ⇒ تحقق مستقل لاستخراج Level 11.
- **جيوب 5/7/9**: مواضع الـPocket objects أبعد ~1–7 سم جوه عن مراكز الـtriggers، وdumps 7 و9 فيها قيم مكررة من dumps تانية (جيوب [1][2] من 11/5، [4][6] مشتركة بين 7 و9) ⇒ **مش معتمدة** كمصدر. مفيش استنتاج منها.
- **لسه مفتوح**: Lvl1-4 وLvl13 مفيهم dumps ⇒ Y افتراض زي ما هو. ابعت نفس الـdump (dump100) وإنت في ترابيزة 1-4 وترابيزة 13 تتقفل.

## دمج الجلسة الموازية (P2Z2) — الفروق بين خط P2W بتاعي وخط V27
اتقارنت هندسة الترابيزات الست (مصدر: `data_1.unity3d` ⇒ `sharedassets0.assets(.resS)`): **نفس القاعدة اتوصّل لها الخطّين لوحدهم** (collider enabled + كل الأجداد active + مش trigger)، ونفس الجيوب والـhulls، ونفس Y/R لكل ترابيزة (local Y للـrack). الفروق الفعلية:
- **اتدمج — Level 13 `Border` ×4 (BoxCollider، material 304، layer 0، tag 0 = untagged):** كانوا `E` (حسب الـmaterial بس) فبيدخلوا في الـrails (10) وفي منطق tag "Edge"، والمحرك بيعرّف `E` = tag Edge (السطر `kind == 2 && hm == 'E' // tag "Edge"`). اتحوّلوا لـ`K` (0.92، من غير منطق Edge) ⇒ rails لـlvl13 = 6. القاعدة الجديدة في `tools/gen_tables.py::letter()`: `E` = tag Edge(20000) + material 303/304؛ `K` = أي material تاني ارتداده 0.92 (303/304/305)؛ `D` = الباقي. البيانات اتولّدت من جديد وطابقت الملفات (48 مثلث بس اتغيّر حرفهم) والاختبار اتعدّل (rails 6).
  **ده قرار من الـtag/الـlayer مش من قياس مرجعي**: لو اللعبة بتعامل الـBorder كـEdge (مثلاً بالـlayer) يتشال التعديل.
- **اتدمج — اختبار جديد في `tests/test_tables.cpp`:** شريحة الكورة [Y−R, Y+R] لازم تتقاطع مع الـcushions (`E`) في كل ترابيزة (بيمسك غلط R/Y).
- **ما اتدمجش (مقصود):** (أ) `lvl5_6` convex rails: خطي كان بيعمل triangulation للـhull (96 مثلث، مساحة 1.778) وV27 بتستخدم مثلثات الـmesh نفسها (88، مساحة 1.761) — الفرق caps أفقية (y=±0.018) مش بتأثر على ضربات الحيط، فسبت V27. (ب) خامة `F` (tag Edge + default material): مش مستخدمة لإن `Edge` box في Level13 غير active. (ج) ملفات `lvl9_12` المكررة (= `level11`). (د) تسمية الجيوب (`CheckBall2_(4)` عندي مقابل `CheckBall2_4`).
- حاجة لقيتها: `data_1.unity3d` فيها `sharedassets0.assets.resS` (15MB)، والـ`tablegroup_*.bundle` textures/sprites بس (مفيش meshes) — مش محتاجينه للهندسة.
