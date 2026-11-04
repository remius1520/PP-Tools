# FINDINGS P2T — قاعدة دخول الجيب (حُسمت من libil2cpp)
## الدليل (فك ARM64)
- `Ball.OnTriggerEnter` (0x5DD1F40) = وصلة: `ldr x8,[x0]; ldr x3,[x8,#0x358]; br x3` ⇒ vtable slot 34 = **`Ball.OnTriggerOccur`** (0x5DD38C8). (ونفس الشي `OnCollisionEnter` 0x5DD38B8 → +0x368 = slot 35 `OnCollisionHappen`.)
- `OnTriggerOccur`: حارس `gameManager+0xAC`؛ ينادي `GetComponent<Icollision>` على GameObject الـtrigger ⇒ `PocketCollision.OnCollisionOccuer(ball)` (0x5D44AF4) اللي بيكتب `ball.targetPocket(+0x180)` ويفعّل `OnTriggerPocket`؛ بيستبعد لو `RulesManager.Foul`؛ ويتحقق من tags (CompareTag) وبيسجّل الـGameObject في +0xE8.
- المصدر = **حدث Unity OnTriggerEnter** ⇒ PhysX trigger = **كورة (sphere) تلمس الـconvex mesh**، مش "المركز داخل الشكل".
## اتغيّر
- `Cfg.pocketSphere=true` افتراضي (`--pocket-center` للقديم): الدخول = أول t تكون فيه مسافة xz بين مركز الكورة والـhull ≤ √(R²−dy²) (dy = بعد Y عن مدى الـmesh). حل: فلتر AABB موسّع + بحث ثلاثي/ثنائي (المسافة دالة محدّبة في t).
- مثال `ball 1 1.0 0.30 0.9 0.45`: دخول CheckBall5 عند 0.1173 ث (sphere) / 0.1521 (مركز-في-hull) / 0.1397 (AABB قديم).
- اختبار `test_pockethull` 5607 check (اتساق).
## افتراضات
- جوانب الـmesh بتتعامل رأسية (prism)؛ الحقيقي: sphere-vs-convex 3D (PhysX). الفرق صغير لأن الـmesh رفيع في Y والكورة بتغطيه كله، بس مش مقاس.
- شروط `OnTriggerOccur` التانية (tags، Foul، +0xAC) مش مطبّقة في المحرك؛ المحرك بيعتبر أي لمس للـtrigger = جيب.
- ترتيب تسليم PhysX لحدث الـtrigger (فريم الـSimulate) مش محسوم ⇒ ممكن فرق tick واحد 0.01 ث.
