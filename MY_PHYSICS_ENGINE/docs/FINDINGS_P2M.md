# FINDINGS P2M (2026-10-31) — بند 2: مين بيملا Queue<CollisionEvent>؟

## النتيجة (موثّق من الباينري)
**مفيش أي كود في libil2cpp بيحط حدث في الـQueue.**
- `Queue<CollisionEvent>.Enqueue` (0x48D5B90) ليه 4 نداءات مباشرة بس: `mpt` 0x5D3CB3C · `iaq` 0x5D3E45C · `jbk` 0x5D41F84 · `AddSolver` 0x5D438F0 (الأربعة نسخ متطابقة: `if activePairs.Contains(MakePairKey(ev.collider1, ev.collider2)) queue.Enqueue(ev)`؛ activePairs = +0x58، queue = +0x60).
- الأربع دوال دي نفسها **مفيش ليها نداء** (مفيش `bl`/`b` ليها في كل libil2cpp — الفحص على الملف كله، `/home/claude/callers.py`)، ومفيش `Action<CollisionEvent>`/`Func`/`UnityEvent` من النوع ده في dump.cs، ومفيش كلاس تاني فيه method بياخد `CollisionEvent`. `OnEnable` (0x5D40E44) بيشترك في 3 أحداث بس (rulesManager + اتنين على gameManager) ومفيهمش الدوال دي.
- النتيجة: `SolveCollisions` (0x5D3D35C) بتلاقي الـQueue فاضي (Count < 1) ⇒ **جسمها مبينفّذش في البناء ده** (ده كود شبه-ميت من الـobfuscation). ده بيقفل سؤال `IsServer` أوفلاين: مش مؤثّر.
- استنتاج (مش مثبت): استدعاء بالـreflection ممكن نظريًا، بس struct كبير مش بيتبعت بـSendMessage/UnityEvent فمستبعد.

## اللي بيحصل فعلًا بعد الاصطدام
- `Ball.OnCollisionEnter` (0x5DD38B8) thunk لـvirtual ⇒ `Ball.OnCollisionHappen` (0x5DD4224): debounce بالوقت (+0x144)، فحص tags، `RegisterCollision` (0x5DD453C)، وبعدين `OnCheckCollisionRPC` (0x5DD30DC) ⇒ `OnCheckCollisionSound`. **مفيهوش كتابة سرعة** (مفيش `set_linearVelocity` في المسار اللي اتقرا) — صوت/أحداث بس.
- الكورة غير البيضا بعد التلامس = **PhysX بس** (ارتداد/impulse من الـmaterials)، مش صيغ SolveCollisions. فالمحرك (impulse على خط المراكز، e=0.92) هو بديل PhysX؛ والدقة الحقيقية محتاجة بند 7/9 (libunity).
- البيضا: `CueBall.OnCollisionEnter` (0x5DD9B7C) ⇒ `ApplySpin` (0x5DD83B4) ⇒ `HandlePhysicsCollisions.SetTargetBallAfterCollision` (0x5D3B350) / `SetCueDirectionAfterBallCollide` (0x5D3B964): ده المسار الحقيقي للتلامس الأول (متنفّذ في المحاكي: first-contact override).

## تأثير على المحرك
مفيش تغيير في الكود. تصحيح للافتراض القديم في `FINDINGS_v5` بند 1/2: `SolveCollisions` مش مسار فعّال ⇒ ما نحاولش نقلّده لتصادم الكور المتتالي.

## لسه مفتوح
- البند 2 اتقفل. اللي فاضل للتصادم المتتالي هو PhysX الحقيقي (بند 7) وenum الدمج (بند 9) من `libunity.so`.
- لم يتم فحص `CueBall.OnCollisionEnter` بالكامل في الجلسة دي لعدم وجود حاجة جديدة فيه (اتفك قبل كده)؛ الفحص الجديد هنا للـnon-cue Ball والـQueue بس.
