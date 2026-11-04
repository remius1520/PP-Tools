# FINDINGS P2S — شكل الجيب الحقيقي (بند 6، جزء) + layer الـpocket triggers (بند 4 أ، جزء)
المصدر: `data.unity3d` → `sharedassets0.assets` (تم فكّه بـ`tools/ufs.py` + `tools/scene.py` + `tools/level_geo.py`). فك ساكن فقط.

## اللي اتحسم (موثّق المصدر)
1. **الـCheckBall triggers مش BoxColliders — هي MeshColliders محدّبة (convex, isTrigger)، tag = 20002 PocketInside، layer = 3** (كل الترابيزات: Lvl1-4، 5&6، 7-8، 9، 9-12، 13). كل mesh = 136 vertex.
2. **layer 3 ∉ castMask 0xC00** (layers 10|11) ⇒ `castPockets=false` ثابت من الملفات (كان افتراض في P2R). مجسّمات «Pocket rests/PocketInside» غير الـtrigger = layer 0، material 305، non-convex، tag PocketInside — برضه برّه الماسك.
3. الـAABB اللي المحرك كان بيستخدمه = bounding box للـmesh. مساحة الـconvex hull (xz) = **~54% من الـAABB للجيوب الركن، ~74–78% لجيوب النص** (`ref/pp2_pocket_hulls_all_tables.json`، كل الترابيزات). بإحداثيات Level 11 الـAABB مطابق لـ`data/level11_pockets.txt` بالظبط.
## اتغيّر في المحرك
- `Pocket.hull` + `data/level11_pocket_hulls.txt` (Table-GameLevel9-12Model)؛ `pocketEntry` بيقص الـsegment (مركز الكورة) على المضلّع المحدّب (Cyrus-Beck) بعد الـAABB. `Cfg.pocketHull=true` افتراضي، `--pocket-aabb` للسلوك القديم. `tools/pack_data.py` بيضيف قسم `HULL` للباينري (متوافق للخلف).
- مثال: `ball 1 1.0 0.30 0.9 0.45` → دخول CheckBall5 عند t=0.1521 (hull) بدل 0.1397 (AABB)، الموضع (1.1306,0.3653) بدل (1.1204,0.3602).
- اختبار `tests/test_pockethull.cpp` (4543 check: الـhull جوه الـAABB، hit بالـhull ⇒ hit بالـAABB، نقطة ركن الـAABB خارج الـhull، round-trip للباينري). اختبارات اتساق، مش مقارنة باللعبة.
## افتراضات / ناقص
- **قاعدة الدخول** لسه "مركز الكورة داخل الشكل في xz، وY ضمن ±R" (نفس قاعدة الـAABB القديمة). الحقيقي في Unity: trigger بيتفعّل لما **كورة (sphere r=0.0275) تتلامس مع الـconvex mesh** (Minkowski بنصف القطر) ⇒ الجيب الفعلي ممكن يكون أوسع بـ~R. ما اتغيّرش من غير مصدر؛ لو عايزه: `grow` بنصف القطر (محتاج حسم: OnTriggerEnter ولا فحص مركز في كود اللعبة — `PocketInside` handler في libil2cpp).
- الـ3D hull (المضلّع في xz هو إسقاط؛ جوانب الـmesh ممكن تكون مائلة) — الـY بيتفحص بـ[ymin−R, ymax+R] زي القديم.
- `Level13Table/PocketInsideTrigger`: pptr الـmesh = 0 (مرجع خارجي/null) ⇒ اتخطّى.
- باقي الترابيزات: الـhull متاح في `ref/pp2_pocket_hulls_all_tables.json` بس المحرك مش بيحمّلها (Level 11 بس). الـrails/الحيطان للترابيزات التانية و Y الكورة لـLvl1_4 لسه.
- محتاج `libunity.so` لبند 7 (PhysX) — مش في المرفوعات.
