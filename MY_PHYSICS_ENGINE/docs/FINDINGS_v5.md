# FINDINGS v5 — فك libil2cpp (جلسة 2026-10-31، من ملفات اللعبة بس)
مفيش بناء ولا تشغيل على جهاز. اللي اتفك بأداة a64dis (ref/p2e_tools) على `libil2cpp.so` المرفوعة + `dump.cs`.

## اتحسم (من الكود)
1. `PhysicsBuilder.Update` (0x5D3D11C) بينادي `SolveCollisions` (0x5D3D35C) **بس لو `NetworkManager.IsServer`** (0x5D3D230). ولو المضيف الأوفلاين مش IsServer، النتايج دي مش بتتطبّق أصلاً (غير محسوم: لازم لوج من الجهاز).
2. `SolveCollisions` بتسحب **حدث واحد** من `Queue<CollisionEvent>` (PhysicsBuilder+0x60) كل نداء. `CollisionEvent` = {Ball collider1 @0, Collider collider2 @8, Vector3 v1 @0x10, Vector3 hitNormal @0x1C} (dump.cs).
3. الحدث ضد Edge: لو `spinActive || !A.isCue` ⇒ `A.velocity = BallToEdgeCollisionDir(v1, hitNormal)` = `(v − 2(v·n)n)·0.92`. البيضا من غير سبين متسابة لـPhysX.
4. الحدث ضد Ball: `angle = acos(n·v̂/(|n||v̂|))` بالدرجات (v̂ = سرعة A بعد PhysX)، `R = BallToBallCollisionDir(B, v1, hitDist=0, angle, n)` = `−n_xz/|n_xz| · |sin(angle)·|v1|| · 0.92` (y=0)، `B.velocity = R`، ولو `spinActive || !A.isCue` ⇒ `A.velocity −= R`. **hitDist = 0 مؤكد** (`movi d3,#0` عند 0x5D3D904).
5. ثابتا 0.92 (ballrestituation / edgerestitution) = `0x3F6B851F` متكرر في static[0], static[4] (.cctor 0x5D3A974). حدود الطول 1e-5 / 1e-15 من .rodata.
6. في 4 نسخ مشوّشة بنفس المنطق: `ftl` 0x5D3EA80، `kyh` 0x5D4238C، `lto` 0x5D42B94، `bgd` 0x5D43D1C (كلها بتنادي نفس الدالتين).
الصيغ اتنقلت لـ`src/pp2_scripted.hpp` (+ `tests/test_scripted.cpp`، 8 PASS) **من غير ما تتربط بـSim::run**.

## لسه مجهول (ليه الصيغ مربوطة مش مفعّلة)
- **مين بيملا الطابور**: `mpt` 0x5D3CB3C / `iaq` / `jbk` / `AddSolver` 0x5D438F0 مفيش ليهم `bl` مباشر ⇒ بيتنادوا بـdelegate/event (لازم نلاقي الاشتراك، غالباً في Ball). من غيره مش عارف `hitNormal` هو normal التلامس ولا عمودي عليه (الفرق بيقلب `sin(angle)` في التصادم المباشر: صفر ولا 1).
- **التوقيت**: الحدث بيتطبّق في `Update` (إطار)، مش في نفس خطوة الفيزياء. لو المحاكاة كلها جوه `OnServerSimulate` في إطار واحد، الحدث ممكن ما يتنفّذش خلالها. لازم نفك `OnServerSimulate` ونشوف بينادي إيه.
- enum الدمج، fixedDeltaTime، ترتيب الـdamping: **لسه من libunity** (اتسلّم بس لسه ماتفكّش). `PhysicsMaterialCombine` مش في dump.cs أصلاً.
- المحاكي لسه بيعامل كل الكور بنفس ارتداد PhysX 0.92 بعد أول تلامس (مش بالسكربت).
