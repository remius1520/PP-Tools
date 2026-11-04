# ref/ — مرجع للقراءة فقط (مش جزء من المحاكي)
- p2e_core_sim/: صيغ أول تلامس (pp2_shot) والسبين (pp2_spin) وقواعد اللعبة (pp2_game) من P2E. تتنقل لـsrc/ بعد مراجعة كل صيغة مقابل libil2cpp.
- p2e_tools/: أدوات قراءة Unity (ufs/sf/sa0) وفك libil2cpp.
- v85_core/predict_core.hpp: الصيغ المفكوكة بس. أي ثابت متعلّم من تسجيل (cueKeep, tanKeep, kSide, kFollow, kDraw, distFactor, cueEdgeKeepN...) مرفوض.
- docs/: handoffs القديمة + جدول النصوص المفكوكة.
- pp2_tables.json: التصدير الأصلي (من sharedassets0.assets) لهندسة الترابيزات الستة. المصدر القديم لـV3_2. **مش بيُقرا** في البناء؛ `tools/gen_tables.py` الحالي بيولّد من data.unity3d مباشرة. محفوظ كمرجع/مقارنة.
- bridge_draft/: مسودة تحويل سجل الأحداث `Result::tev` لمسارات v85 (من V3_2، مش مختبرة جوه التطبيق).
