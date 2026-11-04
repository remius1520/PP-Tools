echo "=== PremiumPath report $(date)"
echo "id: $(id)"
echo "ns: own=$(readlink /proc/self/ns/mnt) init=$(readlink /proc/1/ns/mnt) rootfs_used=${RFS:-own}"
echo "game users=[${USERS:-none}] target_user=${U:-none} pid=${pid:-none} uid=$( [ -n "$pid" ] && stat -c %u /proc/$pid )"
echo "wrap=$(getprop wrap.$PKG) selinux=$(getenforce) ro.debuggable=$(getprop ro.debuggable) sdk=$(getprop ro.build.version.sdk)"
echo "pm path: $(pm path $PKG 2>&1 | head -n 1)"
PP=0; IL=0
if [ -n "$pid" ]; then PP=$(grep -c premiumpath /proc/$pid/maps 2>/dev/null); IL=$(grep -c libil2cpp /proc/$pid/maps 2>/dev/null); fi
echo "maps: premiumpath=$PP il2cpp=$IL"
echo "--- dirs"
for p in $(paths); do [ -d "$p" ] && echo "EXISTS $p" && ls -ldZ "$p"; done
LC=$(logcat -d -s PremiumPath:I 2>/dev/null | awk '/payload loaded/{b=""} {b=b"\n"$0} END{print b}' | sed "/^\$/d" | tail -n 40)   # only the LAST game launch
echo "--- logcat (tag PremiumPath)"
echo "$LC" | tail -n 20
echo "--- crash evidence AFTER the last payload load (older crashes are ignored)"
T0=$(logcat -d -s PremiumPath:I 2>/dev/null | grep "payload loaded" | tail -n 1 | cut -c1-18)
echo "last payload load: ${T0:-none}"
logcat -d -b main,system,crash 2>/dev/null | grep -E "Fatal signal|SIGSEGV|SIGABRT|SIGBUS|am_proc_died|am_crash.*$PKG" | awk -v t="$T0" 'BEGIN{n=0} { ts=substr($0,1,18); if (t=="" || ts>=t) print }' | tail -n 8
echo "--- last 3 tombstones (backtraces; look for libpremiumpath frames)"
for TB in $(ls -t $RFS/data/tombstones/tombstone_* 2>/dev/null | grep -v '\.pb' | head -n 3); do
  echo "tombstone: $TB ($(stat -c %y $TB | cut -c1-19))"
  grep -E "pid:|signal|Abort message|Cause|#0[0-9] pc|premiumpath" "$TB" | head -n 16
done
echo "--- Unity CRASH log (managed + native stack of the last crash)"
logcat -d -s CRASH:E 2>/dev/null | tail -n 60 | cut -c1-220
echo "--- pp_log.txt"
LOGF=""
for p in $(paths); do [ -z "$LOGF" ] && [ -f "$p/pp_log.txt" ] && LOGF="$p/pp_log.txt"; done
if [ -n "$LOGF" ]; then tail -n 25 "$LOGF"; else echo "(not reachable from root - see logcat above)"; fi
echo "=== VERDICT"
if echo "$LC" | grep -q "FAULT during init"; then echo "X a fault happened inside il2cpp during init and was caught (game kept alive): $(echo "$LC" | grep "FAULT during init" | tail -n 1)"
elif echo "$LC" | grep -qE "hooked$|hooked CueBallView"; then echo "OK: hooked. Aim in a match; if no lines appear look for 'fault in draw' above"
elif echo "$LC" | grep -qE "\[P\] perf|\[10\] aim|\[L\] run#"; then echo "OK: running (hook active, the payload is drawing / predicting). A [5] draw skipped: not aiming line only means you are not aiming right now."
elif echo "$LC" | grep -qE "\[X\] (FAULT|fault)|fault in draw|fault inside"; then echo "X the hook ran but crashed inside draw() and disabled itself (see 'fault' above)"
elif echo "$LC" | grep -q "\[[0-9]\]"; then echo "X lib is loaded in the game but stopped at: $(echo "$LC" | grep '\[[0-9]\]' | tail -n 1)"; echo "  [2]=il2cpp api  [3]=class CueBallView  [4]=Unity methods"
elif echo "$LC" | grep -q "dir="; then echo "X lib IS loaded in the game (it logged dir=) but no stage lines: this payload build predates the logcat mirror, or the game was closed in the first seconds. Wait in the match ~30s then Diagnose again."
elif [ -z "$pid" ]; then echo "X game is not running and no payload line in logcat: press Install (launches the game), wait for the menu, then Diagnose"
elif [ "$PP" = 0 ]; then echo "X lib NOT loaded into the running game (wrap=$(getprop wrap.$PKG)): press Install again so the game restarts with the wrap"
else echo "X lib is mapped but silent: no payload line in logcat"; fi
