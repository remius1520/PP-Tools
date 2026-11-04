#!/bin/sh
# Host build + every test (no Android needed).  The APK itself is built by gradle (see .github/workflows/build.yml).
set -e
CXX=${CXX:-g++}; F="-O2 -std=c++17 -Wno-misleading-indentation"
mkdir -p /tmp/pp_build
for t in core suggest break state_read; do $CXX $F -o /tmp/pp_build/test_$t tests/test_$t.cpp; /tmp/pp_build/test_$t | tail -1; done
$CXX $F -o /tmp/pp_build/test_live tests/test_live.cpp && /tmp/pp_build/test_live
$CXX $F -o /tmp/pp_build/test_paths tests/test_paths.cpp && /tmp/pp_build/test_paths
$CXX $F -o /tmp/pp_build/test_unityhost tests/test_unityhost.cpp -ldl && /tmp/pp_build/test_unityhost
$CXX $F -o /tmp/pp_build/test_probe tests/test_probe.cpp -ldl
/tmp/pp_build/test_probe
$CXX $F -o /tmp/pp_build/test_kin tests/test_kin.cpp -ldl
/tmp/pp_build/test_kin
# syntax check of the payload against a stub android/log.h
mkdir -p /tmp/pp_build/stub/android
printf '#pragma once\n#define ANDROID_LOG_INFO 4\nint __android_log_print(int,const char*,const char*,...);\nint __android_log_vprint(int,const char*,const char*,__builtin_va_list);\n' > /tmp/pp_build/stub/android/log.h
$CXX -std=c++17 -fsyntax-only -Wall -Wno-unused -Wno-misleading-indentation -I/tmp/pp_build/stub native/payload.cpp
echo "ALL OK"
