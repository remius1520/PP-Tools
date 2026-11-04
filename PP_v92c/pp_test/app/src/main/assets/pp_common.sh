# prelude: prepended to every script by the app.
# 1) Root from an app can run in an ISOLATED mount namespace (Android 11 data isolation + Magisk "requester"/"isolate" mode):
#    there /data/data/<other game> does not exist, so the game looked "not installed". RFS = /proc/1/root gives init's real view.
# 2) The game can also live on another Android user (dual messenger 95, secure folder 150).
PKG=@PKG@
RFS=""
if [ ! -d /data/data/$PKG ] && [ -d /proc/1/root/data/data/$PKG ]; then RFS=/proc/1/root; fi
USERS=""
for d in $RFS/data/user/*; do
  u=${d##*/}
  if [ "$u" = "0" ]; then t=$RFS/data/data/$PKG; else t=$d/$PKG; fi
  [ -d "$t" ] && USERS="$USERS $u"
done
USERS=$(echo $USERS)
pid=$(pidof $PKG | cut -d' ' -f1)
if [ -n "$pid" ]; then U=$(( $(stat -c %u /proc/$pid) / 100000 )); else U=$(echo $USERS | cut -d' ' -f1); fi
DIRS=""
if [ -n "$U" ]; then
  CD=/data/data/$PKG/cache; [ "$U" != "0" ] && CD=/data/user/$U/$PKG/cache
  DIRS="$CD /storage/emulated/$U/Android/data/$PKG/files /storage/emulated/$U/Android/data/$PKG/cache"
fi
# every place a dir can be reached from: init's view, our view, and the game's own view (/proc/<pid>/root)
paths() { for d in $DIRS; do echo "$RFS$d"; [ -n "$pid" ] && echo "/proc/$pid/root$d"; done; }
