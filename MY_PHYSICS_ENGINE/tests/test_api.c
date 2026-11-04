#include "../api/pp2_api.h"
#include <stdio.h>
#include <math.h>
int main(int argc, char** argv) {
    int fails = 0; if (pp2_load(argc > 1 ? argv[1] : "data")) { puts("FAIL load"); return 1; }
    PP2Ball b[2] = {{0, -0.5, -0.2, 0, 0}, {1, 0.2, -0.2, 0, 0}}; PP2BallOut o[2]; PP2Pred p; double t;
    if (pp2_shoot(b, 2, 0, 1, 0, 2.0, -1, o, &p, &t)) { puts("FAIL shoot"); return 1; }
    if (!(p.valid && p.isBall && p.targetId == 1 && fabs(p.targetVx - 1.355) < 1e-9)) { puts("FAIL pred"); ++fails; } else puts("PASS pred");
    if (!(o[1].x > 0.2 && t > 0)) { puts("FAIL out"); ++fails; } else puts("PASS out");
    int need = pp2_last_path(0, 0); if (need < 1 + 2 * 2) { puts("FAIL path"); ++fails; } else puts("PASS path");
    if (pp2_set("nope", 1) == 0) { puts("FAIL set"); ++fails; } else puts("PASS set");
    /* V3_2 merge: pp2_load_table + pp2_table_info + pp2_detect */
    { char key[32]; double R = 0, Y = 0; int ya = -1;
      if (pp2_load_table(argc > 1 ? argv[1] : "data", "lvl13") || pp2_table_info(&R, &Y, &ya) || R != 0.0235 || ya != 0) { puts("FAIL table_info"); ++fails; } else puts("PASS table_info");
      double pk[12] = {-1.1, 0.4, -1.1, -0.8, 0.0, 0.46, 0.0, -0.8, 1.2, 0.4, 1.2, -0.8};   /* wrong on purpose */
      if (pp2_detect(argc > 1 ? argv[1] : "data", pk, 6, 0.0235, key, 32) == 0) { puts("FAIL detect-negative"); ++fails; } else puts("PASS detect-negative"); }
    printf("api v%d, %d fails\n", pp2_api_version(), fails); return fails;
}
