# FINDINGS P2V — مراجعة CalcMaxSpinValues كاملة (بند 5، اتقفل)
مصدر: فك ARM64 لـ`SpinController.CalcMaxSpinValues` (0x5DA00C4 → 0x5DA058C) مقابل `calcMaxSpinValues` في `src/pp2_spin.hpp`.
## اللي اتطابق (تعليمة بتعليمة)
- D = clamp01(currentDifference(+0x164)/90) ✓ (0x5DA0120..0x5DA0174)؛ H = clamp01((radius − cd·zeroMult(+0x158))/radius) ✓؛ Q = clamp01(D + globalBias(−0.22)) ✓.
- increment = maxInc + (minInc − maxInc)·D ✓؛ decrease = minDec(+0x138) + (maxDec − minDec)·Q ✓؛ afterZero = minAZ + (maxAZ − minAZ)·H ✓؛ side = maxSide + (minSide − maxSide)·D ✓.
- length = |vector| (من الـsingleton +0x28/2C/30)، surface = length − cueBallRadius(CueBall+0x25C) ✓.
- maxUp(+0x108) = B + (type==2 ? maxIncEdge·B : increment·surface) ✓.
- maxLeft(+0x10C): leftInc(+0xFC)<0 ⇒ (H>0 ? afterZero·surface : decrease·B)؛ وإلا type==2 ⇒ 0.5·maxUp، غير كده B + side·surface ✓.
- maxBottom(+0x114): type==2 ⇒ B·maxDecEdge(+0x154) بدون ما يبص على bottomInc؛ غير كده bottomInc(+0x104)<0 ⇒ نفس الـenvelope، وإلا B + increment·surface ✓.
- maxRight(+0x110): rightInc(+0x100)<0 ⇒ الـenvelope؛ وإلا type==2 ⇒ 0.5·maxBottom، غير كده B + side·surface ✓.
## اللي اتضاف للتوثيق (مفيش أثر فيزيائي)
- ذيل الدالة: لو `+0x39` (IsOwner) true ⇒ بيحط الأربع قيم في float[4] ويحسب أكبرها وينادي event على `+0x170` (UI). المحرك عنده `maxAll` بنفس المعنى؛ ما فيش كتابة في أي حالة فيزيائية.
## الحالة
- مفيش تغيير في كود المحرك. الاختبارات زي ما هي (13/13 · 10/10 · 96 · 24 · 22 · 8 · 18 · 16 · api 0).
- ما اتقارنتش بضربة مرجعية: "موثّق المصدر" بس.
