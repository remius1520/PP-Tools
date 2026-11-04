#!/bin/sh
# Host build + all tests.  Android: see CMakeLists.txt / .github/workflows/build.yml (same sources, no dependencies).
set -e
CXX=${CXX:-g++}
$CXX -O2 -std=c++17 -Wall -Wextra -Wno-misleading-indentation -o pp2_sim src/pp2_cli.cpp
$CXX -O2 -std=c++17 -Wno-misleading-indentation -o test_engine tests/test_engine.cpp
./test_engine data
# P2X: packed-binary round trip for test_hull3d (needs python3; skipped otherwise)
(command -v python3 >/dev/null && python3 tools/pack_data.py data pp2_data_test.bin /dev/null >/dev/null) || true
for t in solvers spin postcollision diff_A clientpredict spinwire servertracker special pockethull tables hull3d merge endshot sleepwake; do
  $CXX -O2 -std=c++17 -Wno-misleading-indentation -o test_$t tests/test_$t.cpp
  ./test_$t data | tail -1
done
$CXX -O2 -std=c++17 -Wno-misleading-indentation -shared -fPIC -o libpp2_sim.so api/pp2_api.cpp
${CC:-gcc} -O2 -o test_api tests/test_api.c -L. -lpp2_sim -lm -Wl,-rpath,'$ORIGIN'
./test_api data
# P2Z: live predictor (mock host) + il2cpp host (stub API)
$CXX -O2 -std=c++17 -Wno-misleading-indentation -o test_live tests/test_live.cpp
./test_live
$CXX -O2 -std=c++17 -Wno-misleading-indentation -o test_il2cpp_host tests/test_il2cpp_host.cpp android/pp2_il2cpp_host.cpp -ldl
./test_il2cpp_host
rm -f pp2_data_test.bin
echo "OK -> ./pp2_sim, ./libpp2_sim.so"
