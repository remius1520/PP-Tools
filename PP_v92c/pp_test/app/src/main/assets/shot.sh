# copies pp2_shot.txt (written by the payload inside the game) to Download and prints a short summary.
F=""
for p in $(paths); do [ -z "$F" ] && [ -f "$p/pp2_shot.txt" ] && F="$p/pp2_shot.txt"; done
if [ -z "$F" ]; then echo "X مفيش pp2_shot.txt: اضغط \"تجهيز التسجيل\" وانت جوه ماتش، واضرب ضربة، وبعدين اضغط حفظ."; exit 0; fi
cp "$F" /sdcard/Download/pp2_shot.txt 2>/dev/null && echo "saved Download/pp2_shot.txt ($(wc -c < "$F") bytes)"
if grep -q "^END" "$F"; then echo "OK: الضربة اتسجلت كاملة"; grep "^END\|^SHOT" "$F" | cut -c1-300; else echo "! الضربة لسه ما خلصتش أو ما اتسجلتش (مفيش سطر END). استنى الكور توقف وجرّب تاني."; head -n 3 "$F" | cut -c1-200; fi
