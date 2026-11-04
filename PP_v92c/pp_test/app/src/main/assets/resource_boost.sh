#!/system/bin/sh
# Resource Boost & Performance Manager (v1.1)
# بيحفظ القيمة الأصلية لـ oom_score_adj قبل ما يغيّرها، عشان زر "إلغاء" يرجّعها.

PKG="com.PoolLiveTour"
DIR=/data/local/tmp/pp_test
LOG_FILE="$DIR/boost_log.txt"
ORIG="$DIR/oom_orig.txt"
mkdir -p "$DIR" 2>/dev/null

echo "--- Starting Boost ---" > $LOG_FILE
date >> $LOG_FILE

PID=$(pidof $PKG | cut -d' ' -f1)
if [ -n "$PID" ] && [ -w /proc/$PID/oom_score_adj ]; then
    CUR=$(cat /proc/$PID/oom_score_adj 2>/dev/null)
    # احفظ الأصل مرة واحدة بس (لو الزر اتداس مرتين ما نكتبش فوق الأصل بـ -1000)
    if [ ! -s "$ORIG" ] || [ "$(cut -d' ' -f1 "$ORIG")" != "$PID" ]; then
        echo "$PID $CUR" > "$ORIG"
    fi
    echo "-1000" > /proc/$PID/oom_score_adj 2>/dev/null
    echo "PID $PID: oom_score_adj $CUR -> $(cat /proc/$PID/oom_score_adj 2>/dev/null)" >> $LOG_FILE
else
    echo "WARN: اللعبة مش شغالة (افتحها الأول)" >> $LOG_FILE
fi

rm -rf $DIR/temp_cache/* 2>/dev/null
echo "Boost complete!" >> $LOG_FILE
cat $LOG_FILE
