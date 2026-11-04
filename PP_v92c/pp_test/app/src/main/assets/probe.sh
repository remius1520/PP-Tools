# copies pp2_probe.txt (written by the payload inside the game) to Download and prints it.  @T@ = unix time of the button press.
T0=@T@
F=""
for p in $(paths); do [ -z "$F" ] && [ -f "$p/pp2_probe.txt" ] && F="$p/pp2_probe.txt"; done
if [ -z "$F" ]; then
  if [ -z "$pid" ]; then echo "X اللعبة مش شغالة: اضغط تثبيت (بيشغّل اللعبة)، استنى المنيو، وجرّب تاني."
  else echo "X مفيش pp2_probe.txt: الأداة مش محمّلة في اللعبة؟ اضغط تثبيت وابدأ اللعبة من جديد."; fi
  exit 0
fi
M=$(stat -c %Y "$F" 2>/dev/null || echo 0)
cp "$F" /sdcard/Download/pp2_probe.txt 2>/dev/null && echo "saved Download/pp2_probe.txt"
if [ "$M" -lt "$T0" ]; then echo "! الملف ده من تشغيل اللعبة (قبل الضغطة). ادخل ماتش (لازم الخط يشتغل) واضغط تاني لو عايز فحص جديد."; fi
cat "$F"
