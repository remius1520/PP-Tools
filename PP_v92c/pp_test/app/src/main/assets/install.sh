echo "[1] game users=[${USERS:-none}] rootfs=${RFS:-own} running pid=${pid:-none} target user=${U:-none}"
if [ -z "$U" ]; then U=0; echo "WARN: game dir not visible from root, assuming user 0 (pm path: $(pm path $PKG 2>&1 | head -n 1))"; fi
DST=/data/local/tmp/pp_test/libpremiumpath.so
mkdir -p /data/local/tmp/pp_test
# v25: the game must NOT be running while the lib changes, and the file must be replaced by RENAME (new inode): copying over a mapped .so truncates it under the running
# process => SIGBUS in libpremiumpath.so (pp_report_12, 13:33, backtrace #00 inside libpremiumpath.so right after __pthread_start).
am force-stop --user $U $PKG; sleep 1
cp "@LIB@" $DST.new || { echo "ERROR step 2: cannot copy @LIB@"; exit 1; }
mv -f $DST.new $DST || { echo "ERROR step 2b: cannot replace $DST"; exit 1; }
chmod 755 $DST
chcon u:object_r:system_file:s0 $DST 2>&1 | head -n 1
echo "[2] lib: $(ls -lZ $DST)"
setprop wrap.$PKG LD_PRELOAD=$DST
echo "[3] wrap.$PKG=$(getprop wrap.$PKG)  ro.debuggable=$(getprop ro.debuggable)  selinux=$(getenforce)"
am force-stop --user $U $PKG
ACT=$(cmd package resolve-activity --brief --user $U -c android.intent.category.LAUNCHER $PKG 2>/dev/null | tail -n 1)
echo "[4] launching '$ACT' for user $U"
if [ -z "$ACT" ] || [ "${ACT#*/}" = "$ACT" ]; then echo "ERROR step 4: launcher activity not found for user $U"; exit 1; fi
am start --user $U -n "$ACT" 2>&1 | tail -n 2
i=0; npid=""
while [ $i -lt 20 ] && [ -z "$npid" ]; do sleep 1; npid=$(pidof $PKG | cut -d' ' -f1); i=$((i+1)); done
echo "[5] game pid=${npid:-NOT RUNNING}"
if [ -n "$npid" ]; then
  sleep 4
  c=$(grep -c premiumpath /proc/$npid/maps 2>/dev/null)
  if [ "$c" -gt 0 ] 2>/dev/null; then echo "[6] OK: libpremiumpath.so is loaded inside the game"; else echo "[6] FAIL: lib NOT loaded. Android ignores wrap.* on non-debuggable builds (ro.debuggable=$(getprop ro.debuggable)) -> the ptrace injector is needed"; fi
fi
