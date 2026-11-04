# CHANGES v3 (فوق v2) — 2026-10-31

## اتعمل (كله من فك libil2cpp / ملفات اللعبة)
1. `src/pp2_solvers.hpp` — حلّال التداخل: `SolveWithCueBall` 0x5D3F86C · `SolveBallWithCushions` 0x5D3FB10 · `SolveBallWithBall` 0x5D3FE64 · ترتيب `SolveBalls` 0x5D40FA4. مدمج في `Sim::run` بعد انتهاء الحركة (positions بس).
   - ثوابت مقروءة من .rodata: safeval 1.2e-4 (0x1319B38) · معامل الحيطة 0.6 (0x1318940) · minLen 1e-5 (0x1319D60). solverValue = 2·contactOffset + 1.2e-4 = 0.00212، edgeSolver = 0.0006.
   - الحيطان = `Collider.ClosestPointOnBounds` يعني **AABB** لكل collider مش المش نفسه. Level 11: 6 rails (مكوّنات متصلة من مثلثات E).
2. `src/pp2_postcollision.hpp` + `src/pp2_spin.hpp` منقولين للـsrc (24 + 96 اختبار). ApplySpin مبيحسبش سبين، بيطبّق سجل نتيجة جاهز.
3. التاجات: `dx()` = "Edge"، التاج المستثنى في SetCueDirectionAfterBallCollide = "PocketInside" (من TAGS_AND_BRANCHES_R5 + decoded strings).
4. ثوابت: الزاوية العالية 265 (اتأكدت من 0x1318C5C)، smoothTime 0.03 (0x1319A50).

## افتراضات (مكتوبة، مش مثبّتة)
- Ball.radius (+0xB4) = 0.0275: قيمة الـprefab مقرتش.
- عدد التمريرات 8: من MASTER_8، مفيش `bl` مباشر لـSolveBalls اتلاقى (بيتنادى غير مباشر).
- static Vector3 عند [class+0xB8] (fallback الاتجاه): مجهول، الافتراض صفر. ولو كورتين على نفس المركز اللعبة بتستخدم Random.insideUnitSphere (غير حتمي).
- enum الدمج: Unity العام Average0/Multiply1/Minimum2/Maximum3 ⇒ 2 = Minimum ⇒ 0.92. **من معرفة Unity العامة، مش من libunity** (الـenum مش في dump.cs).

## لسه
- `PhysicsBuilder.SolveCollisions` 0x5D3D35C (1900 byte): اتقرا جزء (Edge/Ball tag، BallToEdge/BallToBallCollisionDir، MakePairKey)، مش متنفذ.
- libunity: fixedDeltaTime، ترتيب الـdamping، PhysX نفسه.
- قيم الستيك من الـbundle، سلسلة CalcMaxSpinValues كاملة، باقي الترابيزات.
- كل ده بدون مقارنة بضربة مرجعية: "موثّق المصدر" مش "مطابق".
