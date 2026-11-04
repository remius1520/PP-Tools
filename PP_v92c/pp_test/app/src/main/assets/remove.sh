setprop wrap.$PKG ''
[ -n "$U" ] && am force-stop --user $U $PKG
echo "removed (user=${U:-none}) - next launch is vanilla"
