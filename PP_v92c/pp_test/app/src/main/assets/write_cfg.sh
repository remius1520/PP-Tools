if [ -z "$U" ]; then echo "ERROR: game not found for any user (rootfs=${RFS:-own} users=[$USERS])"; exit 1; fi
ok=0
for p in $(paths); do
  if [ -d "$p" ]; then
    f="$p/pp_cfg.txt"; touch "$f" 2>/dev/null
    printf '@BODY@' > "$f.new"
    # v20: keep every key the app does not manage (spin / power offsets, gaps, ...) - only the keys in this body are replaced
    for k in $(cut -d= -f1 "$f.new"); do grep -v "^$k=" "$f" > "$f.tmp" 2>/dev/null; cat "$f.tmp" > "$f"; done
    cat "$f.new" >> "$f"; rm -f "$f.new" "$f.tmp"; chmod 666 "$f" && echo "cfg -> $p" && ok=1
  fi
done
[ $ok = 0 ] && echo "ERROR: could not write pp_cfg.txt (user=$U pid=${pid:-none} rootfs=${RFS:-own})"
