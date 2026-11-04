> **تحديث P2P (محسوم من libunity):** قيمة الدمج 2 = **Minimum** (جدول 0x209e34 → PxCombineMode eMIN). الارتداد الفعلي 0.92. تفاصيل: docs/FINDINGS_P2P.md. سطر "enum الدمج في Unity: Average=0, Minimum=1, Multiply=2" تحت **غلط** (الصح: Average0/Multiply1/Minimum2/Maximum3 في ملفات اللعبة).

> **تصحيح (v2):** عمود/جملة "enum الدمج: Multiply=2" تحت **غير مؤكدة وغالباً غلط**. الأرجح enum Unity: Average=0, Multiply=1, Minimum=2, Maximum=3 → الارتداد الفعلي 0.92 مش 0.8464 (متفق مع كود اللعبة). لسه محتاج تأكيد من libunity.so.

# قيم الفيزياء المقروءة مباشرة من ملفات اللعبة (Unity 6000.3.9f1)
المصدر: data.unity3d → globalgamemanagers / sharedassets0.assets (فك ساكن، بدون تشغيل اللعبة ولا تسجيل ضربات).

## PhysicsManager (globalgamemanagers، class 55) — مؤكد
| الحقل | القيمة |
|---|---|
| gravity | (0, -9, 0) |
| bounceThreshold | 0 |
| defaultMaxDepenetrationVelocity | 10 |
| sleepThreshold | 0.001 |
| defaultContactOffset | 0.001 |
| solverIterations / solverVelocityIterations | 30 / 20 |
| defaultMaxAngularSpeed | 7 (مرجّح، من آخر الـblob) |

## PhysicMaterial (sharedassets0، class 134) — مؤكد
| الاسم | dynFric | statFric | bounciness | frictionCombine | bounceCombine |
|---|---|---|---|---|---|
| Ball_Physics | 0 | 0 | 0.92 | 2 (Multiply) | 2 (Multiply) |
| EdgePhysics_Snooker | 0 | 0 | 0.92 | 2 | 2 |
| Edges_Physics | 0 | 0 | 0.92 | 2 | 2 |
| Pocket_Physics | 0 | 0 | 0.92 | 2 | 2 |
| Table_Physics | 0 | 0 | 0 | 3 (Maximum) | 2 (Multiply) |

enum الدمج في Unity: Average=0, Minimum=1, Multiply=2, Maximum=3.

## Rigidbody الكورة (class 54، عدد 2) — مؤكد
mass 10، linearDamping 0.62، angularDamping 1.0، centerOfMass (0,0,0)،
inertiaTensor (1,1,1)، inertiaRotation (0,0,0,1)، useGravity=1، isKinematic=0،
interpolate=1، constraints=4 (FreezePositionY)، collisionDetection=2 (ContinuousDynamic).

## SphereCollider الكورة (class 135، عدد 2) — مؤكد
radius 0.5 (قبل scale الـTransform)، center (0,0,0)، material = Ball_Physics.

## ملاحظات
- المجموع في sharedassets0: MeshCollider ×237، BoxCollider ×6، SphereCollider ×2، Rigidbody ×2، PhysicMaterial ×5.
- TimeManager: الـfixedDeltaTime مخزّن بصيغة RationalTime (count=1411199 على base مرجّح 141120000 ≈ 0.01 ثانية) — استنتاج، يحتاج تأكيد من libunity.so.
- لسه مش متأكد منها: frictionType، solverType (PGS/TGS)، enhancedDeterminism، autoSimulation في PhysicsManager.

## المشهد (sharedassets0.assets) — مؤكد من scene.py
- الكورتين: `Solo (1)` (layer 8) و`BallBase` (layer 10)، scale عالمي 0.055 ⇒ نصف القطر الفعلي = 0.5 × 0.055 = **0.0275** (قطر 55 مم).
- layerCollisionMatrix في PhysicsManager: كل الـ32 صف = 0xFFFFFFFF (كل الـlayers بتتصادم مع بعض).
- مجموعات الـMeshColliders (237 collider، 96 mesh مختلف):
  - الحيطان: layer 11، tag 20000، material = Edges_Physics (304)؛ منها 8 convex والباقي non-convex.
  - مجموعات كتير material = null ⇒ Unity بيستخدم الـdefault material (PhysicsManager.defaultMaterial = null) وقيمته المعروفة 0.6/0.6/0 بدمج Average. لازم نتأكد لكل Level هل ده سطح الترابيزة ولا مجسّمات شكلية.
  - في triggers (BoxCollider/MeshCollider بـisTrigger=1) للجيوب وغيرها.
- MeshCollider: cookingOptions = 30 (EnableMeshCleaning + WeldColocatedVertices + CookForFasterSimulation + UseFastMidphase).
- الملف الكامل لكل collider (transform عالمي، material، layer، tag، mesh id): scene_sharedassets0.json.
