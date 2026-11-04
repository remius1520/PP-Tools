# FINDINGS P2U — شروط Ball.OnTriggerOccur (بند 4 أ، جزء) — فك ARM64 لـ0x5DD38C8
## التسلسل (موثّق المصدر)
1. حارس: `gameManager(+0x198)` غير null و **`GameManager.isHit` (+0xAC، `<isHit>k__BackingField`)** = true، وإلا خروج. (الاسم من dump.cs سطر 979330.)
2. لو `IsClient`(+0x3E) و`ballService`(+0x120) ≠ null ⇒ `BallService.OnTriggerClient(other.gameObject)` (جهة العميل بس).
3. لو `IsServer`(+0x3A) = false ⇒ خروج. (يعني المنطق ده سيرفر بس.)
4. `other.GetComponent<Icollision>()` ⇒ لو موجود ينادي `OnCollisionOccuer(this)` (PocketCollision 0x5D44AF4، P2T).
5. لو RulesManager موجود و`Foul` = true ⇒ خروج.
6. فلتر tags: ثلاث CompareTag على `other.gameObject` من static fields لكلاس (offsets +0x10 و+0x18 و+0x0)؛ لو الـGetComponent (خطوة 5-أ، slot 0x268) رجّع non-null بيتخطّى مقارنة +0x10. لو ولا tag اتطابق ⇒ خروج.
7. `LasHitObject(+0xE8)=other.gameObject`؛ لو في `CollidedObjects(+0x118)` بالفعل ⇒ خروج (دخول واحد لكل collider)، وإلا يتضاف.
## أثر على المحرك
- الحارس `isHit`: افتراض إنه true أثناء الضربة (مش محسوم هوية اللي بيكتبه) ⇒ مفيش تغيير.
- de-dup (خطوة 7) متوافق مع المحرك (أول دخول بس).
- IsServer بس ⇒ مطابق لنموذج "السيرفر بينفّذ".
## هوية الـtags (اتحسمت)
- الكلاس = `TagConstant` (dump.cs سطر 1355585): CUTOUT +0x0، POCKET +0x8، BALL +0x10، EDGE +0x18. الـ.cctor (0x5DF6950) بيملّاهم من `a.g/h/i/j` (كلاس `a` TypeDef 18368، `a_(idx,off,len)`، g ⇒ off 0x40 len 6).
- فك blob (metadata offset 0x9BF478، طول 0x330، `out[i]=in[i]^(i&0xFF)^0xAA`): عند 0x40 = `Cutout` `PocketInside` `Ball` `Edge`. يعني **CUTOUT="Cutout"، POCKET="PocketInside"، BALL="Ball"، EDGE="Edge"**.
- فلتر الخطوة 6 بيقبل Ball / Edge / Cutout **ومش PocketInside**. الجيب بيتعالج في الخطوة 4 (`OnCollisionOccuer`) قبل فحص Foul والـtags، وتسجيل `LasHitObject/CollidedObjects` ده للكور/الحيط/الـcutout بس.
- ملحوظة: الخطوة 4 قبل فحص Foul في الكود ⇒ استبعاد Foul لازم يتفحص جوه `PocketCollision.OnCollisionOccuer` نفسها (P2T قال كده) — مش متحقّق هنا.
## كاتب isHit و slot 0x268 (اتحسموا)
- `GameManager.isHit`(+0xAC) = **true** بيتكتب في `ServerTracker.OnInitalizeSimulation` (0x5DB7454) و`OnInitializeShotRPC` (0x5DB8B74) ونسختين obfuscated (`ewz` 0x5DB8A4C، `nga` 0x5DB9ACC) عن طريق `ServerTracker.gameManager`(+0x158)؛ و**false** بيتكتب في `GameManagerService.OnResetHitRPC` (0x5DC45A0) و3 نسخ obfuscated (0x5DC3AC4، 0x5DC5488، 0x5DC7780) عن طريق `GameManagerService.gameManager`(+0xF0). يعني guard الخطوة 1 = "الضربة اتبدأت ولسه ما اتعملهاش reset" ⇒ **true طول محاكاة الضربة** (افتراض المحرك اتأكد).
- slot 0x268 = `GetComponent<CueBall>` (نفس الـslot في `StickService.ShotTheBall` بيقرا بعده CueBall+0x220). فمنطق الخطوة 6: لو الـcollider مش CueBall ⇒ يتحقق من tag Ball أولًا؛ لو CueBall يتخطّى Ball ويتحقق Edge ثم Cutout بس. يعني البيضا نفسها مش بتتسجّل في `LasHitObject` بـtag Ball. (مفيش أثر على الجيب.)
## افتراضات / لسه مفتوح
- مفيش. مفيش تغيير في كود المحرك؛ الاختبارات زي ما هي (13/13 · 10/10 · 96 · 24 · 22 · 8 · 18 · 16 · api 0).
