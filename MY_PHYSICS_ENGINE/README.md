# PP2 simulator (V29 = V28/P2Z2 + V3_2 merge) — محاكي بيليارد من ملفات اللعبة (6 ترابيزات)
`sh build.sh` يبني ويشغّل كل الاختبارات. الناتج: `pp2_sim` (CLI) و`libpp2_sim.so`.
مثال: `printf 'ball 0 -0.5 -0.2\nball 1 0.2 -0.2\nshot 0 1 0 2.0\n' | ./pp2_sim --data data --table level11 --no-path`
(`shot cue dirx dirz F [spin]` = ضربة اللعبة بقواعد السيرفر؛ `ball id x z vx vz` = مشهد عادي. الترابيزات: lvl1_4 lvl5_6 lvl7_8 lvl9 level11 lvl13.)
C ABI (`api/pp2_api.h`، v3): `pp2_load_table` · `pp2_detect` (تعرّف الترابيزة من الجيوب ونصف القطر) · `pp2_table_info` · `pp2_shoot` · `pp2_simulate` · `pp2_last_path`.
C++: `Sim::run(balls, shot, wantPath, wantEv)` — `wantEv=true` بيملّى `Result::tev` (حيطة/كورة/جيب/وقوف) لبناء المسارات.
GitHub: جذر الزيب = جذر الريبو: فُكّه وارفع كل اللي جواه على ريبو جديد (`.github/workflows/build.yml` في الجذر). الـActions بتشتغل مع كل push (أو Run workflow): job `host` (g++ + كل الاختبارات) ثم `android` (NDK arm64: `libpp2_sim.so` + `pp2_sim` + `libpp2_il2cpp_host.a` + `data/`). artifacts: `pp2-sim-android-arm64`، ولوجات `pp2-host-log` و`pp2-android-log` (اتبعتلي اللوج لو وقع).
ابدأ من `PP2_MASTER_36.md` ثم `docs/FINDINGS_P2Z3.md`. `ref/` مرجع فقط. ما اتقارنش بضربة حقيقية: "موثّق المصدر" مش "مطابق".
