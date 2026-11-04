# copies the payload's log (and the previous one) to Download so it can be sent for analysis
n=0
for p in $(paths); do
  for f in pp_log.txt pp_log.txt.old; do
    if [ -f "$p/$f" ]; then cp "$p/$f" /sdcard/Download/$f 2>/dev/null && echo "saved Download/$f ($(wc -c < "$p/$f") bytes) from $p" && n=$((n+1)); fi
  done
  [ $n -gt 0 ] && break
done
[ $n = 0 ] && echo "no pp_log.txt found (open the game once with the tool installed, aim in a match, then try again)"
echo "--- last 25 lines"
for p in $(paths); do [ -f "$p/pp_log.txt" ] && tail -n 25 "$p/pp_log.txt" && break; done
