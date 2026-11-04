# FINDINGS P2K (2026-10-31) — بند 6 (قيم الستيك) : اتحسم مصدرها

## اتحسم (من ملفات اللعبة)
- `cuegroup_assets_all_*.bundle` = بصريات بس (MonoBehaviour فيه itemAddress/Itemid + sprites/animators): **مفيش فيه قيم سبين/باور**.
- قيم الستيك موجودة كـTextAsset JSON اسمه **`cueitemsConfig`** جوه `resources.assets` (من data.unity3d)، 138 ستيك: `itemid` + `itemdata{ aimhelperRange, spinfactorDividor, maxStickPowerPercentege, ... }`. (في كمان `itemsConfig` لسه ما اتفتحش.)
- `CueItem` (dump.cs): الحقول `Nullable<float>` (spinfactorDividor +0x50، maxStickPowerPercentege +0x48، aimhelperRange +0x58) بـ`[JsonProperty]` وقيم متدرّجة بخطوة 0.125.
- الـgetter `CueItem.get_SpinFactorDividor` (0x5DF8A88، من فك ARM64): `hasValue ? value : 0.125` ⇒ الحقل الناقص = **0.125** (const defval).
- مدخلات `mapTipToAngleWithCueItem.cueSpinFactorDividor` = القيمة دي للستيك المستخدم.

## الأرقام (138 ستيك)
قيم spinFactorDividor الفعلية (بعد الـdefault): 0.75 (54 ستيك) · 1 (44) · 0.125 (15، منهم 12 ناقصة ⇒ default) · 0.375 (11) · 0.625 (4) · 0.5 (3) · 0.875 (3) · 0 (2) · 0.25 (2). الجدول الكامل: `data/cueitems_effective.csv` + الخام `data/cueitems_config.json`.

## لسه مفتوح (مش اتحسم هنا)
- أي ستيك بيستخدمه اللاعب في اللعب الأوفلاين (مين الـdefault؟ `defaultcue`/`isDefaultCue` مش في الـJSON ده).
- `externalBaseline` و`left/right/bottomIncrement` و transform vector لـ`CalcMaxSpinValues`: مش من الستيك؛ لسه محتاجين libil2cpp (مصدرها SpinController/singleton).
- ربط `evaluateSpinPipeline` بالمحاكي ما اتعملش (فاضل المدخلات اللي فوق).
