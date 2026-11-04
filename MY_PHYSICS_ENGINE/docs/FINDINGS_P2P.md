# FINDINGS P2P (2026-10-31) — بند 9 (enum الدمج) محسوم، بند 4 محسوم جزئيًا

## بند 9: enum دمج الارتداد/الاحتكاك — محسوم من libunity.so
المسار:
1. المرسِل: 0x781e8c بيقرا من PhysicMaterial: +0x34/+0x38/+0x3c (احتكاك/ارتداد) و+0x40/+0x44 (frictionCombine/bounceCombine، خام) ويبعتهم بأوامر 0x400000D و0x400000F عبر مؤشر الباك-إند (.bss 0x16f47d8).
2. كاتب المؤشر: 0x7812ac (`BackendSystem::Select`) = أول مدخل في جدول دوال الباك-إند المختار.
3. دالة توزيع PhysX backend: 0xc02f30 (type = ((id>>26)-1)&0x3f، index = id&0x3ffffff). جدول type-0 في .bss 0x176b908 بيتملّى من 0xc09264؛ index 15 (0x400000F) = **0xc09174**.
4. 0xc09174: لكل قيمة v: `v-1 <= 2 ? table[v-1] : 0` بجدول int32 في rodata **0x209e34 = {2, 1, 3}**؛ الأولى → vtable+0x88 (setFrictionCombineMode)، التانية → +0x98 (setRestitutionCombineMode). (index 13 = 0xc090d0 بيكتب dynFric/statFric/restitution عبر +0x40/+0x50/+0x60: تخطيط PxMaterial متسق.)
النتيجة (`tools/libunity/combine_table.py`): ملف 0→Average، 1→Multiply(Px 2)، **2→Minimum (Px 1)**، 3→Maximum.
- مصدر ترتيب PxCombineMode (Average0/Min1/Multiply2/Max3): معرفة عامة (header PhysX)، مش من الباينري؛ الجدول نفسه من الباينري.
- كل الـmaterials فيها 2 (Table_Physics: friction 3=Max، bounce 2=Min) ⇒ ارتداد حيطة = min(0.92,0.92)=**0.92**؛ سطح من غير ارتداد = min(·,0)=0. ده اللي المحرك بيعمله. **0.8464 مرفوض.**
- لسه ما حصلتش مقارنة بضربة مرجعية: "موثّق المصدر" مش "مطابق".

## بند 4: سكربتات اللعبة والـsub-steps — محسوم جزئيًا
- `UnityEngine.Time` في الباينري فيه getters بس (set_fixedDeltaTime/set_timeScale اتشالوا بالـstripping) ⇒ السكربتات على الأرجح مبتغيّرهمش (استنتاج).
- `ServerTracker.OnNetworkSpawn` (0x5db5bfc): لو السيرفر ⇒ `Physics.simulationMode = 2` (Script). يعني الفيزياء بتتشغّل يدويًا.
- `ServerTracker.FixedUpdate` (0x5db99f4): لو IsServer ⇒ `OnServerSimulate` (0x5db856c). مفيش نداء تاني ليها.
- `OnServerSimulate`: s8=Time.fixedDeltaTime، N=`substeps` (+0x140).
  - عادي: لو isSolving(+0x14C)=false ⇒ `Physics.Simulate(fixedDeltaTime)` **مرة واحدة**؛ لو true ⇒ مفيش Simulate.
  - خاص (بيضا موجودة، !InTube، حقل CueBall+0x10b=0، CueBall+0x26c=0، `onSimulate`(+0x110)=true): السيرفر بيكتب `linearVelocity` للبيضا كل خطوة (target = قاعدة(+0xD4..) + اتجاه·مقدار − موضع حالي). بعدها: لو فلاج +0x108 (حقل مش ظاهر في dump) ⇒ `do { Simulate(fixedDelta/N) } while(N > i)` يعني N استدعاء؛ وإلا لو المسافة < `stopSimulateThreshold`(+0x144) ⇒ تثبيت الموضع وتصفير السرعة وonSimulate=false؛ ثم Simulate(fixedDelta) مرة.
- ⇒ **الـ7 sub-steps مش ثابتة**: بتحصل بس في المسار الخاص لما +0x108 يكون true، وقيمتها حقل متسلسل `substeps`.
- `Physics.Simulate` ليه نداءين تانيين في `frq` (0x5db6674) ما اتفكّوش.

## لسه مفتوح (افتراض)
- قيمة `substeps`/`stopSimulateThreshold` المتسلسلة وهوية فلاج +0x108: مسحت مشاهد data.unity3d وكل الـbundles بحث بأنماط float ورجّع ضوضاء بس؛ المطلوب parser typetree لـMonoBehaviour بتاع ServerTracker (غالبًا prefab سيرفر مش موجود في الملفات المرفوعة، أو monoscripts bundle).
- المحرك لسه على 7 sub-steps (افتراض) ومش بيفرّق عادي/خاص.
- بند 5 (Euler(spin).y عند 180°): ما اتبدأش.
