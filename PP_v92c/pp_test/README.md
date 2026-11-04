# PremiumPath v84 LIVE
APK يحقن مكتبة جوه اللعبة (root + `wrap.<pkg>` LD_PRELOAD) بترسم الخطوط جوه اللعبة (مفيش أوفر لاي).
`sh build.sh` = اختبارات الكمبيوتر. الـAPK بيتبني بـgradle (`.github/workflows/build.yml`: NDK 26 + CMake 3.22).
التفاصيل: `docs/HANDOFF_v84.md`. مراجع PP2: `docs/PP2_MASTER_32.md`, `docs/FINDINGS_P2Z.md`.
الملفات: `native/` (payload.cpp, unityhost.hpp, live.hpp, paths.hpp, il2cpp.hpp) · `core/` (محرك v83 للتقدير والاقتراح) · `app/` (Kotlin + scripts) · `tests/`.
