# FINDINGS P2F — مراجعة مستقلة للسبين (2026-10-31)
فكّيت نفس دوال SpinController **بشكل مستقل** من `libil2cpp.so` (من غير ما أبص على فك V5)، وبعدين قارنت الاتنين على 20000 حالة عشوائية لـ`calcMaxSpinValues` و40000 لـ`applySpinFactorCombination` و`spin angle` (`tests/test_diff_A.cpp`، نسختي في `ref/pp2_spin_A_independent.hpp`). الفروق اللي طلعت اتأكدت واحدة واحدة من تعليمات ARM64 وتصلّحت هنا:

| # | الدالة | كان في V5 | الصح (دليل ARM64) |
|---|---|---|---|
| 1 | CalcMaxSpinValues | `currentDifference / 180` | **/ 90** (`movz w8,#0x42B4,lsl#16` = 90.0f عند 0x5DA0120، مش 180 = 0x4334) — بيأثر على الأوزان الـ3 |
| 2 | CalcMaxSpinValues | `decrease = minBaseIncrementValue(+0x130) + …` | **minBaseDecreaseRate (+0x138)**: الخانة `[sp+0x14]` بتتخزّن فيها 0x130 وبعدين تتكتب فوقها بـ0x138 (0x5DA0278) قبل ما تتقرا (0x5DA0330) |
| 3 | CalcMaxSpinValues | `maxBottom = B·maxBaseDecreaseEdge` لكل الأنواع | Edge (type 2) بس: `B·maxDecEdge` من غير ما يبص على bottomIncrement؛ غير كده: `bottomInc<0 ? envelope : B + increment·surface` (0x5DA03B8..0x5DA0434) |
| 4 | CalcMaxSpinValues | `maxRight = B + maxBaseDecreaseEdge·surface` | **B + side·surface** (0x5DA049C؛ s3 = side، الـ0x154 بيتحمّل بس في مسار Edge) |
| 5 | GetSpinAngelMapping | `atan2(vertical-term, horizontal-term)` | **atan2(horizontal-term, vertical-term)**: arg0 (s0) = `-(kH·signed + h)/D`، arg1 (s1) = `-(kV·cd + v)/D`. الاتنين كانوا معكوسين ⇒ الزاوية كانت `90° − الزاوية` |
| 6 | GetSpinAngelMapping | الصفر → `ballAngelFullRange (+0x124)` | الصفر → **ثابت rodata 0.1** (`0x3DCCCCCD` عند 0x1319840)؛ الحقل بيتكتب بعدها مش بيتقرا |
| 7 | (تسمية) | `externalBaseline` "كلاس مجهول" | **`GamePhysics.Instance.physicsBuilder.finalVelocity`** (GamePhysics.physicsBuilder @0x20، PhysicsBuilder @0x40). والمتجه (+0x28/2C/30) = `PhysicsBuilder.forceDirection`، و`cueBallRadius` (CueBall+0x25C) هو `hitDistance` مش نصف قطر |
| 8 | (تسمية) | `globalBias` | float rodata = **-0.22** |

اختبارات V5 اللي كانت بتثبّت القيم الغلط اتصلّحت (currentDifference 90→45 مع zeroMult 0.05→0.1 عشان الأوزان تفضل زي ما هي، والـmaxRight/maxBottom/decrease/angle/spinFactor الجديدة محسوبة بإيدي ومتطابقة مع نسختي المستقلة).

## غير محسوم (صريح)
- الـthunk رقم 0x7067330 (Atan2؟) و0x7067320 مش متسمّيين في الباينري: ترتيب الوسائط في (5) مبني على إنه `Mathf.Atan2(y, x)` والـs0 هو الأول. لو طلع غير كده الزاوية تتقلب.
- `FINDINGS_v5` بند 1: الحدث بيتطبّق بس لو `IsServer` — لسه من غير تأكيد للأوفلاين.
