#include "pp2_api.h"
#include "../src/pp2_engine.hpp"
#include <cstring>
using namespace pp2;
static Sim g_sim; static bool g_loaded = false; static std::vector<double> g_path;
static int fill(const Result& r, PP2BallOut* out, double* tEnd) {
    for (size_t i = 0; i < r.balls.size(); ++i) out[i] = {r.balls[i].id, r.balls[i].x, r.balls[i].z, r.balls[i].pocketed ? 1 : 0, r.balls[i].tPocket};
    if (tEnd) *tEnd = r.t; g_path.clear(); for (auto& p : r.path) { g_path.push_back(p.t); for (auto& q : p.p) { g_path.push_back(q.x); g_path.push_back(q.z); } } return 0;
}
extern "C" {
int pp2_api_version(void) { return PP2_API_VERSION; }
int pp2_load(const char* d) { g_loaded = d && g_sim.load(d); return g_loaded ? 0 : 1; }
int pp2_load_table(const char* d, const char* key) { g_loaded = d && key && g_sim.loadTable(d, key); return g_loaded ? 0 : 1; }
int pp2_detect(const char* d, const double* pocketXZ, int n, double radius, char* keyOut, int cap) {
    if (!pocketXZ || n <= 0) return 1;
    std::vector<V2> pk; for (int i = 0; i < n; ++i) pk.push_back({pocketXZ[2 * i], pocketXZ[2 * i + 1]});
    std::string k = Sim::detect(d ? d : "", pk, radius);
    if (keyOut && cap > 0) { strncpy(keyOut, k.c_str(), cap - 1); keyOut[cap - 1] = 0; }
    return k.empty() ? 1 : 0;
}
int pp2_table_info(double* R, double* Y, int* yAssumed) {
    if (!g_loaded) return 1; if (R) *R = g_sim.C.R; if (Y) *Y = g_sim.C.Y;
    if (yAssumed) *yAssumed = (g_sim.table && g_sim.table->ySourced) ? 0 : 1; return 0;
}
int pp2_set(const char* k, double v) {
    Cfg& c = g_sim.C; std::string s = k ? k : "";
    if (s == "R") c.R = v; else if (s == "Y") c.Y = v; else if (s == "drag") c.drag = v; else if (s == "dragMode") c.dragMode = (int)v;
    else if (s == "angularDrag") c.angularDrag = v; else if (s == "eBall") c.eBall = v; else if (s == "eEdge") c.eEdge = v; else if (s == "ePocket") c.ePocket = v; else if (s == "eDefault") c.eDefault = v;
    else if (s == "stop") c.stop = v; else if (s == "ballStop") c.ballStop = v; else if (s == "tmax") c.tmax = v; else if (s == "sleepKE") c.sleepKE = v; else if (s == "sleepTime") c.sleepTime = c.wakeCounter = v; else if (s == "wakeCounter") c.wakeCounter = v;
    else if (s == "ccdMode") c.ccdMode = (int)v; else if (s == "maxDepenetrationVelocity") c.maxDepenetrationVelocity = v;
    else if (s == "firstContact") c.firstContact = v != 0; else return 1; return 0;
}
int pp2_shoot(const PP2Ball* b, int n, int cue, double dx, double dz, double F, double spin, PP2BallOut* out, PP2Pred* pred, double* tEnd) {
    if (!g_loaded || !b || !out || n <= 0) return 1;
    std::vector<Ball> B; for (int i = 0; i < n; ++i) B.push_back({b[i].id, b[i].x, b[i].z, b[i].vx, b[i].vz});
    Shot s; s.cueId = cue; s.dx = dx; s.dz = dz; s.F = F; s.spin = spin; Result r = g_sim.run(B, &s, true);
    if (pred) { const Prediction& p = r.pred; *pred = {p.valid, p.isBall, p.targetId, p.hitDistance, p.ballAngleDeg, p.cueV.x, p.cueV.z, p.targetV.x, p.targetV.z}; }
    return fill(r, out, tEnd);
}
int pp2_simulate(const PP2Ball* b, int n, PP2BallOut* out, double* tEnd) {
    if (!g_loaded || !b || !out || n <= 0) return 1;
    std::vector<Ball> B; for (int i = 0; i < n; ++i) B.push_back({b[i].id, b[i].x, b[i].z, b[i].vx, b[i].vz});
    return fill(g_sim.run(B, nullptr, true), out, tEnd);
}
int pp2_last_path(double* buf, int cap) { int need = (int)g_path.size(); if (buf) for (int i = 0; i < need && i < cap; ++i) buf[i] = g_path[i]; return need; }
}
