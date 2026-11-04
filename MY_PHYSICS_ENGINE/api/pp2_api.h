/* PP2 simulator C ABI (stable, plain C).  Load once, then call pp2_shoot / pp2_simulate from any thread-confined caller. */
#ifndef PP2_API_H
#define PP2_API_H
#ifdef __cplusplus
extern "C" {
#endif
#define PP2_API_VERSION 3   /* 3 = v2 + pp2_detect + pp2_table_info (V3_2 merge; additive, v2 callers unchanged) */
typedef struct { int id; double x, z, vx, vz; } PP2Ball;
typedef struct { int id; double x, z; int pocketed; double tPocket; } PP2BallOut;
typedef struct { int valid, isBall, targetId; double hitDistance, ballAngleDeg, cueVx, cueVz, targetVx, targetVz; } PP2Pred;

int pp2_api_version(void);
int pp2_load(const char* dataDir);                       /* 0 = ok.  dataDir contains level11_solids.txt + level11_pockets.txt */
int pp2_load_table(const char* dataDir, const char* tableKey);  /* lvl1_4 lvl5_6 lvl7_8 lvl9 level11 lvl13 -> loads <key>_*.txt and sets R,Y preset (P2W) */
/* Which table is on screen?  pocketXZ = n game pockets as x,z pairs; radius = ball radius.  0 = found, key copied to keyOut (e.g. "level11");
   1 = no match: do NOT use the simulator.  Reads <key>_pockets.txt from a text data dir. */
int pp2_detect(const char* dataDir, const double* pocketXZ, int n, double radius, char* keyOut, int cap);
/* R, Y of the loaded table.  yAssumed is 1 only if the ball height was NOT sourced from the game (all six tables are sourced since P2X, so 0 after pp2_load_table; 1 after plain pp2_load). */
int pp2_table_info(double* R, double* Y, int* yAssumed);
int pp2_set(const char* key, double value);              /* keys: R Y drag dragMode eBall eEdge ePocket eDefault stop tmax sleepKE sleepTime firstContact ; 0 = ok */
/* The game's shot: cue gets dir*F, first contact follows the game's server rules.  spin<0 = centred tip.  out has n entries. 0 = ok. */
int pp2_shoot(const PP2Ball* balls, int n, int cueId, double dx, double dz, double F, double spin, PP2BallOut* out, PP2Pred* pred, double* tEnd);
/* Plain scene (no first-contact logic). */
int pp2_simulate(const PP2Ball* balls, int n, PP2BallOut* out, double* tEnd);
/* Path of the last call, every 0.01 s: frames of [t, x0, z0, x1, z1, ...] (1+2n doubles).  Returns the number of doubles needed; copies up to cap. */
int pp2_last_path(double* buf, int cap);
#ifdef __cplusplus
}
#endif
#endif
