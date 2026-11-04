// PremiumPath v84 LIVE payload (runs INSIDE the game process; OFFLINE / vs-computer matches).
// What it does, in one paragraph: it hooks CueBallView.LateUpdate (method-pointer swap, no code patched).  While the player aims it draws, with cloned in-game LineRenderers
// (NO Android overlay window, no UDP, nothing outside the game):
//   * SOLID lines = the EXACT path: the game's own Physics.Simulate (snapshot -> the game's shot rules -> simulate tick by tick -> restore), PP2 P2Z.
//   * FAINT lines = a quick geometric preview (friction-free long path from the old v83 engine) while the exact path is not ready (aim moving / not pulling) or unavailable.
// Fallback chain (all in this one build, no rebuild needed): exact live path -> faint preview -> nothing (the game's own aim line is never touched).
#include "il2cpp.hpp"
#include "unityhost.hpp"
#include "pp2probe.hpp"
#include "paths.hpp"
#include "../core/predict_core.hpp"
#include "../core/suggest.hpp"
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <climits>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>

#define TAG "PremiumPath"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

struct Vec3 { float x, y, z; };
struct Col { float r, g, b, a; };
using namespace il2;

// ---------- field offsets from dump.cs (this game build).  Names are resolved first (fieldOff), these are the fallbacks ----------
namespace off {
    constexpr int CBV_trajectoryHandler = 0x20, CBV_cueBall = 0x28, CBV_gameManager = 0x30, TH_targetLine = 0x40;
    constexpr int CB_scaledDirection = 0x1B0, CB_cueRay = 0x1F4, CB_isAiming = 0x26D, CB_ghost = 0x260, CB_collided = 0x26C;
    constexpr int B_radius = 0xB4, B_color = 0xF8, B_isCue = 0x109, B_insidePocket = 0x10B;
    constexpr int GM_balls = 0xB8, GM_isHit = 0xAC;
    constexpr int PB_forceDir = 0x28, PB_targetVel = 0x44, PB_levelMax = 0x48, PB_cushions = 0x50;
    constexpr int CB_afterCollided = 0x220, CB_targetDir = 0x22C, CB_collidedBall = 0x218;
    constexpr int UO_cachedPtr = 0x10, L_items = 0x10, L_size = 0x18, ARR_data = 0x20, ARR_len = 0x18;
}
template <class T> static inline T rd(const void* base, int o) { return *(const T*)((const char*)base + o); }
static inline bool plausible(const void* p) { return (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x0000800000000000ULL; }
static inline bool alive(const void* uobj) { return plausible(uobj) && plausible(rd<void*>(uobj, off::UO_cachedPtr)); }
static double nowSec() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

// field offset by NAME (il2cpp reflection), fallback = the constant from dump.cs
static int fieldOff(Klass* k, const char* name, int fallback) {
    if (!k || !il2cpp_class_get_field_from_name || !il2cpp_field_get_offset) return fallback;
    void* f = il2cpp_class_get_field_from_name(k, name); if (!f) return fallback;
    const int o = (int)il2cpp_field_get_offset(f); return (o > 0 && o < 0x2000) ? o : fallback;
}

// ---------- config (written by the app with root into the game's cache dir: pp_cfg.txt, re-read every second) ----------
struct Cfg {
    int on = 1;
    int live = 1;            // 0 = exact path off, 1 = on (falls back to the faint preview), 2 = exact only (no faint preview while the aim moves)
    int draft = 1;           // 1 = faint geometric preview (idle / while the aim is moving / when the exact path is unavailable)
    int suggest = 1;         // 9-ball shot + break suggestion (v81/v82 planner, approximate engine: it is a SUGGESTION, the solid line is the truth)
    int nummap = 1;          // list index -> real ball number for the 9-ball planner
    int rec = 0;             // v87: app writes a new number here to arm the one-shot recorder (pp2_shot.txt)
    int probe = 0;           // v87: app writes a new number here to ask for the PP2 name probe (pp2_probe.txt)
    int ff = 1;              // v86: analytic fast-forward of the pre-contact ticks, verified against one real Physics.Simulate tick first (0 = always full Physics.Simulate)
    int gate = 1;            // mute the game's own collision/trigger callbacks while the predictor runs (no double ApplySpin / sound / events)
    int diag = 1;            // extra log lines (throttled): first device builds keep this on
    int livedir = 0;         // exact path direction: 0 = PhysicsBuilder.forceDirection (what the game fires), 1 = CueBall.scaledDirection
    int colormode = 0;       // 0 = ball colours, 1 = by role
    int markers = 1, ballrings = 1, rings = 1;
    double settle = 0.10, gap = 0.25;          // exact path runs when the input has been still for `settle` s, at most every `gap` s
    double livet = 4.0, livems = 60.0;         // simulated seconds / wall-clock budget (ms) of one exact run
    double width = 0.011, lift = 0.012, ringw = 0.4, fade = 0.45, taper = 0.25, longalpha = 0.40;
    double maxlines = 70;
    // faint preview engine (v83 core)
    int bounces = 3, chain = 4;
    double inset = 0.055, keep = 0.92, ballkeep = 0.92, corner = 0.09, mid = 0.07, gapc = 0.11, gapm = 0.07, longdrag = 0.03, vmax = 4.3, usecol = 1;
};
static Cfg g_cfg;
static std::string g_dir;
static bool probeDir(const std::string& d) { std::string f = d + "/.pp_probe"; FILE* x = fopen(f.c_str(), "w"); if (!x) return false; fclose(x); unlink(f.c_str()); return true; }
// The game may run as another Android user (dual app / secure folder): probe the candidates, first writable wins.
static const std::string& dirNow() {
    if (!g_dir.empty()) return g_dir;
    const char* pkg = "com.PoolLiveTour"; int n = (int)(getuid() / 100000); char b[256]; std::vector<std::string> c;
    snprintf(b, sizeof b, "/data/data/%s/cache", pkg); c.push_back(b);
    snprintf(b, sizeof b, "/data/user/%d/%s/cache", n, pkg); c.push_back(b);
    snprintf(b, sizeof b, "/storage/emulated/%d/Android/data/%s/files", n, pkg); c.push_back(b);
    snprintf(b, sizeof b, "/storage/emulated/%d/Android/data/%s/cache", n, pkg); c.push_back(b);
    snprintf(b, sizeof b, "/sdcard/Android/data/%s/files", pkg); c.push_back(b);
    for (auto& d : c) if (probeDir(d)) { g_dir = d + "/"; LOG("dir=%s uid=%d", g_dir.c_str(), (int)getuid()); return g_dir; }
    static int warned = 0; if (!warned++) LOG("no writable dir found, uid=%d", (int)getuid());
    return g_dir;
}
static void logFile(const char* fmt, ...) {
    { va_list a2; va_start(a2, fmt); __android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, a2); va_end(a2); }
    if (dirNow().empty()) return;
    const std::string p = g_dir + "pp_log.txt"; static int n = 0;
    if ((++n & 63) == 0) { struct stat st; if (stat(p.c_str(), &st) == 0 && st.st_size > 600 * 1024) rename(p.c_str(), (p + ".old").c_str()); }   // keep the log small: the newest 600 KB + the previous 600 KB
    FILE* f = fopen(p.c_str(), "a"); if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
}
static void loadCfg() {
    if (dirNow().empty()) return;
    FILE* f = fopen((g_dir + "pp_cfg.txt").c_str(), "r"); if (!f) return;
    char k[64]; double v; Cfg c = g_cfg;
    struct Kv { const char* k; double* v; };
    Kv tab[] = {{"settle", &c.settle}, {"gap", &c.gap}, {"livet", &c.livet}, {"livems", &c.livems}, {"width", &c.width}, {"lift", &c.lift}, {"ringw", &c.ringw}, {"fade", &c.fade}, {"taper", &c.taper}, {"longalpha", &c.longalpha},
        {"maxlines", &c.maxlines}, {"inset", &c.inset}, {"keep", &c.keep}, {"ballkeep", &c.ballkeep}, {"corner", &c.corner}, {"mid", &c.mid}, {"gapc", &c.gapc}, {"gapm", &c.gapm}, {"longdrag", &c.longdrag}, {"vmax", &c.vmax}, {"usecol", &c.usecol}};
    struct Ki { const char* k; int* v; };
    Ki ti[] = {{"on", &c.on}, {"live", &c.live}, {"draft", &c.draft}, {"suggest", &c.suggest}, {"nummap", &c.nummap}, {"gate", &c.gate}, {"ff", &c.ff}, {"probe", &c.probe}, {"rec", &c.rec}, {"diag", &c.diag}, {"livedir", &c.livedir}, {"colormode", &c.colormode},
        {"markers", &c.markers}, {"ballrings", &c.ballrings}, {"rings", &c.rings}, {"bounces", &c.bounces}, {"chain", &c.chain}};
    while (fscanf(f, " %63[^=]=%lf", k, &v) == 2) {
        bool hit = false; for (auto& e : ti) if (!strcmp(k, e.k)) { *e.v = (int)v; hit = true; break; }
        if (!hit) for (auto& e : tab) if (!strcmp(k, e.k)) { *e.v = v; break; }
    }
    fclose(f);
    c.bounces = std::max(0, std::min(12, c.bounces)); c.chain = std::max(0, std::min(6, c.chain)); c.maxlines = std::max(10.0, std::min(150.0, c.maxlines));
    c.settle = std::max(0.02, std::min(1.0, c.settle)); c.gap = std::max(0.05, std::min(3.0, c.gap)); c.livet = std::max(0.5, std::min(12.0, c.livet)); c.livems = std::max(10.0, std::min(250.0, c.livems));
    g_cfg = c;
}

// ---------- resolved Unity functions (lines) ----------
static const Method *mGetTransform, *mGetPosition, *mInstantiate, *mFindObjs, *mSetCount, *mSetPos, *mSetC0, *mSetC1, *mSetWorld;
static const Method *mGetGO, *mSetActive, *mRendSetEn, *mSetSW, *mSetEW, *mGetBounds, *mGetComp;
static Klass *kPhysicsBuilder, *kGameIn, *kSpin, *kPocket, *kBall, *kCueBall, *kGM, *kRbG;
static void* callObj(const Method* m, void* self) { return ((void* (*)(void*, const Method*))fnptr(m))(self, m); }
static bool getPos(void* comp, Vec3& out) {
    if (!alive(comp)) return false;
    void* tr = callObj(mGetTransform, comp); if (!alive(tr)) return false;
    out = ((Vec3 (*)(void*, const Method*))fnptr(mGetPosition))(tr, mGetPosition); return true;
}

// ---------- crash guard: any fault inside our drawing code disables the feature instead of killing the game ----------
static sigjmp_buf g_jb; static volatile sig_atomic_t g_in = 0; static volatile long g_tid = 0; static struct sigaction g_old;
static sigjmp_buf g_jbI; static volatile sig_atomic_t g_inI = 0; static volatile long g_tidI = 0; static volatile int g_stage = 0;   // separate guard for the init thread
static volatile uintptr_t g_fPc = 0, g_fAddr = 0; static volatile int g_dstage = 0; static uintptr_t g_il2Base = 0;
static volatile bool g_inPredict = false;     // true only while live::predict runs (the gated game callbacks check it)
static inline long tidNow() { return (long)syscall(SYS_gettid); }
static void onSegv(int sig, siginfo_t* si, void* ctx) {
#if defined(__aarch64__)
    if (g_inI && tidNow() == g_tidI) { g_fPc = (uintptr_t)((ucontext_t*)ctx)->uc_mcontext.pc; g_fAddr = (uintptr_t)si->si_addr; }
#endif
    if (g_inI && tidNow() == g_tidI) siglongjmp(g_jbI, 1);
#if defined(__aarch64__)
    if (g_in && tidNow() == g_tid) { g_fPc = (uintptr_t)((ucontext_t*)ctx)->uc_mcontext.pc; g_fAddr = (uintptr_t)si->si_addr; }
#endif
    if (g_in && tidNow() == g_tid) siglongjmp(g_jb, 1);      // only the thread that is inside our hook: a fault on another thread must not jump into our frame
    static thread_local int depth = 0;                       // Unity's handler chains back to us and we chain to it: this guard stops an endless bounce
    if (depth > 0) { signal(sig, SIG_DFL); raise(sig); return; }
    ++depth;
    if (g_old.sa_flags & SA_SIGINFO) { if (g_old.sa_sigaction && g_old.sa_sigaction != onSegv) { g_old.sa_sigaction(sig, si, ctx); --depth; return; } }
    else if (g_old.sa_handler && g_old.sa_handler != SIG_IGN && g_old.sa_handler != SIG_DFL) { g_old.sa_handler(sig); --depth; return; }
    --depth; signal(sig, SIG_DFL); raise(sig);
}
// Unity installs its OWN SIGSEGV handler on top of ours: check every 2 s and re-install on top (v25).
static void ensureHandler() {
    struct sigaction cur; if (sigaction(SIGSEGV, nullptr, &cur) != 0) return;
    if ((cur.sa_flags & SA_SIGINFO) && cur.sa_sigaction == onSegv) return;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = onSegv; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    struct sigaction prev; if (sigaction(SIGSEGV, &sa, &prev) == 0) { g_old = prev; static int n = 0; if (n++ < 5) logFile("[X] SIGSEGV handler had been replaced (by the game); ours re-installed on top (#%d)", n); }
}

// ---------- line pool (v78 shadow cache: a call into the game only when the value really changed) ----------
struct Line { void* lr = nullptr; uint32_t gch = 0; };
static std::vector<Line> g_pool; static void* g_poolOwner = nullptr; static void* g_template = nullptr; static double g_drawAfter = 0; static void* g_lrKlass = nullptr;
static inline bool validLine(void* lr) { return alive(lr) && g_lrKlass && rd<void*>(lr, 0) == g_lrKlass; }
static const Col WHITE{1, 1, 1, 1};
struct LShadow { int cnt = -1; std::vector<Vec3> pts; Col col{-9, -9, -9, -9}, col2{-9, -9, -9, -9}; float w = -9, w2 = -9; double visT = -1e9; };
static std::unordered_map<void*, LShadow> g_sh; static void* g_shLr = nullptr; static LShadow* g_shP = nullptr;
static inline LShadow& shOf(void* lr) { if (lr != g_shLr || !g_shP) { g_shP = &g_sh[lr]; g_shLr = lr; } return *g_shP; }
static void shClear() { g_sh.clear(); g_shLr = nullptr; g_shP = nullptr; }
static void shErase(void* lr) { g_sh.erase(lr); g_shLr = nullptr; g_shP = nullptr; }
static std::vector<Line> g_pending; static volatile bool g_skipDrain = false;   // clones parked after a fault: only their GC handle is dropped, they are never touched
static void drainPending() { int n = 0; while (!g_pending.empty() && n++ < 64) { Line l = g_pending.back(); g_pending.pop_back(); if (l.gch && il2cpp_gchandle_free) il2cpp_gchandle_free(l.gch); } }
static void setCount(void* lr, int n) {
    LShadow& s = shOf(lr); if (s.cnt == n) return;
    ((void (*)(void*, int, const Method*))fnptr(mSetCount))(lr, n, mSetCount);
    s.cnt = n; s.pts.assign(n > 0 ? (size_t)n : 0, Vec3{NAN, NAN, NAN});
}
static void setPos(void* lr, int i, Vec3 v) {
    LShadow& s = shOf(lr);
    if (i >= 0 && i < (int)s.pts.size()) { Vec3& q = s.pts[i]; if (q.x == v.x && q.y == v.y && q.z == v.z) return; q = v; }
    ((void (*)(void*, int, Vec3, const Method*))fnptr(mSetPos))(lr, i, v, mSetPos);
}
static void setColor2(void* lr, Col c, Col c1) {
    LShadow& s = shOf(lr); auto same = [](const Col& a, const Col& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
    if (same(s.col, c) && same(s.col2, c1)) return; s.col = c; s.col2 = c1;
    ((void (*)(void*, Col, const Method*))fnptr(mSetC0))(lr, c, mSetC0); ((void (*)(void*, Col, const Method*))fnptr(mSetC1))(lr, c1, mSetC1);
}
static void setColor(void* lr, Col c) { setColor2(lr, c, c); }
static void hideAll() { for (auto& l : g_pool) if (validLine(l.lr)) setCount(l.lr, 0); }
// a cloned line really visible: active GameObject, enabled renderer, explicit width
static void forceVisible(void* lr, float w, float w2 = -1.f) {
    LShadow& sv = shOf(lr); const double tv = nowSec(); const float w2e = w2 > 0.f ? w2 : w;
    if (sv.w == w && sv.w2 == w2e && tv - sv.visT < 2.0) return;
    sv.w = w; sv.w2 = w2e; sv.visT = tv;
    if (mGetGO && mSetActive) { void* go = callObj(mGetGO, lr); if (alive(go)) ((void (*)(void*, bool, const Method*))fnptr(mSetActive))(go, true, mSetActive); }
    if (mRendSetEn) ((void (*)(void*, bool, const Method*))fnptr(mRendSetEn))(lr, true, mRendSetEn);
    if (mSetSW) ((void (*)(void*, float, const Method*))fnptr(mSetSW))(lr, w, mSetSW);
    if (mSetEW) ((void (*)(void*, float, const Method*))fnptr(mSetEW))(lr, w2e, mSetEW);
}
// the clones are managed objects only our native code points to: a strong GC handle keeps them alive; one Unity destroyed (scene change) is re-created in place
static void* makeClone(void* templ, uint32_t& gch) {
    void* c = ((void* (*)(void*, const Method*))fnptr(mInstantiate))(templ, mInstantiate);
    if (!alive(c)) return nullptr;
    shErase(c); gch = (il2cpp_gchandle_new && plausible(c)) ? il2cpp_gchandle_new((Obj*)c, false) : 0;
    ((void (*)(void*, bool, const Method*))fnptr(mSetWorld))(c, true, mSetWorld);
    forceVisible(c, (float)g_cfg.width); return c;
}
static void releasePool() { shClear(); for (auto& l : g_pool) if (l.gch && il2cpp_gchandle_free) il2cpp_gchandle_free(l.gch); g_pool.clear(); }
static void* ensureLine(size_t i, void* templ) {
    while (g_pool.size() <= i) { uint32_t g = 0; void* c = makeClone(templ, g); if (!c) return nullptr; g_pool.push_back({c, g}); }
    if (!validLine(g_pool[i].lr)) {
        if (g_pool[i].gch && il2cpp_gchandle_free) il2cpp_gchandle_free(g_pool[i].gch);
        uint32_t g = 0; void* c = makeClone(templ, g); g_pool[i] = {c, g}; if (!c) return nullptr;
    }
    return g_pool[i].lr;
}

// ---------- game object finders (cached, re-looked-up every 2 s) ----------
static void* firstInstance(Klass* k) {
    if (!mFindObjs || !fnptr(mFindObjs) || !k) return nullptr;
    Obj* t = il2cpp_type_get_object(il2cpp_class_get_type(k));
    void* arr = ((void* (*)(void*, int, const Method*))fnptr(mFindObjs))(t, 0, mFindObjs);
    if (!plausible(arr) || rd<int>(arr, off::ARR_len) < 1) return nullptr;
    void* o = rd<void*>(arr, off::ARR_data); return alive(o) ? o : nullptr;
}
static void* findOne(Klass* k, void*& cache, double& next) {
    if (alive(cache)) return cache; if (nowSec() < next) return nullptr; next = nowSec() + 2.0; cache = firstInstance(k); return cache;
}
static void* g_pb = nullptr; static double g_pbNext = 0; static void* g_spin = nullptr; static double g_spinNext = 0; static void* g_gin = nullptr; static double g_ginNext = 0;
static void* findPB() { return findOne(kPhysicsBuilder, g_pb, g_pbNext); }
static float readReel() {   // GameInpusHandler.reelPosition (+0x3C): the live power-slider pull (v70f/g)
    if (!kGameIn) return -999.f; void* g = findOne(kGameIn, g_gin, g_ginNext); return g ? rd<float>(g, 0x3C) : -999.f;
}
// pockets: the 6 Pocket MonoBehaviours; position = orbitPoint (v16)
static std::vector<pp::V2> g_pk; static double g_pkNext = 0; static void* g_pkFor = nullptr;
static std::vector<pp::V2> readPockets(void* gm) {
    if (g_pkFor == gm && g_pk.size() >= 4 && nowSec() < g_pkNext) return g_pk;
    if (g_pkFor != gm) { g_pk.clear(); g_pkFor = gm; }
    g_pkNext = nowSec() + 3.0;
    if (!mFindObjs || !kPocket) return g_pk;
    Obj* tobj = il2cpp_type_get_object(il2cpp_class_get_type(kPocket));
    void* arr = ((void* (*)(void*, int, const Method*))fnptr(mFindObjs))(tobj, 0, mFindObjs);
    if (!plausible(arr)) return g_pk; int n = rd<int>(arr, off::ARR_len); if (n < 1 || n > 64) return g_pk;
    bool got[8] = {false}; pp::V2 pos[8]; int cnt = 0;
    for (int i = 0; i < n; ++i) {
        void* po = rd<void*>(arr, off::ARR_data + 8 * i); if (!alive(po)) continue;
        int loc = rd<int>(po, 0x20); if (loc < 1 || loc > 6 || got[loc]) continue;
        void* orb = rd<void*>(po, 0x28); Vec3 p; if (!(alive(orb) && getPos(orb, p)) && !getPos(po, p)) continue; got[loc] = true; pos[loc] = {p.x, p.z}; ++cnt;
    }
    if (cnt >= 4) { g_pk.clear(); for (int l = 1; l <= 6; ++l) if (got[l]) g_pk.push_back(pos[l]); }
    return g_pk;
}
// measured cushion limits (v14): the real cushion colliders -> ball-centre limits for the faint preview
struct Bnd { float c[3], e[3]; };
struct Meas { bool ok = false; double xmin = 0, xmax = 0, zmin = 0, zmax = 0; };
static Meas g_meas; static void* g_measFor = nullptr; static int g_measTries = 0; static double g_measNext = 0;
static bool deriveLimits(const std::vector<Bnd>& bs, const std::vector<pp::V2>& pk, double rad, Meas& m) {
    double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9; for (auto& q : pk) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); z0 = std::min(z0, q.z); z1 = std::max(z1, q.z); }
    double cx = (x0 + x1) * 0.5, cz = (z0 + z1) * 0.5; std::vector<double> xl, xh, zl, zh;
    for (auto& b : bs) { double ex = b.e[0], ez = b.e[2], x = b.c[0], z = b.c[2];
        if (ex > 1.5 * ez) { if (z < cz) zl.push_back(z + ez); else zh.push_back(z - ez); } else if (ez > 1.5 * ex) { if (x < cx) xl.push_back(x + ex); else xh.push_back(x - ex); } }
    if (xl.empty() || xh.empty() || zl.empty() || zh.empty()) return false;
    auto med = [](std::vector<double>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    Meas r; r.xmin = med(xl) + rad; r.xmax = med(xh) - rad; r.zmin = med(zl) + rad; r.zmax = med(zh) - rad;
    if (!(r.xmin - x0 > -0.02 && r.xmin - x0 < 0.25 && x1 - r.xmax > -0.02 && x1 - r.xmax < 0.25 && r.zmin - z0 > -0.02 && r.zmin - z0 < 0.25 && z1 - r.zmax > -0.02 && z1 - r.zmax < 0.25)) return false;
    r.ok = true; m = r; return true;
}
static void measure(void* gm, const std::vector<pp::V2>& pk, double cr) {
    if (g_measFor == gm || g_measTries >= 8 || nowSec() < g_measNext || !mGetBounds) return; g_measNext = nowSec() + 2.5; ++g_measTries;
    void* pb = findPB(); if (!pb) return; void* cl = rd<void*>(pb, off::PB_cushions);
    void* it = plausible(cl) ? rd<void*>(cl, off::L_items) : nullptr; int n = plausible(cl) ? rd<int>(cl, off::L_size) : 0; std::vector<Bnd> bs;
    if (plausible(it) && n > 0 && n <= 16) for (int i = 0; i < n; ++i) { void* col = rd<void*>(it, off::ARR_data + 8 * i); if (!alive(col)) continue; bs.push_back(((Bnd (*)(void*, const Method*))fnptr(mGetBounds))(col, mGetBounds)); }
    Meas m; if (!bs.empty() && deriveLimits(bs, pk, cr, m)) { g_meas = m; g_measFor = gm; logFile("[6] measured ball-centre limits x[%.4f,%.4f] z[%.4f,%.4f] (r=%.4f, %d cushion colliders)", m.xmin, m.xmax, m.zmin, m.zmax, cr, (int)bs.size()); }
}
// ball -> Rigidbody: Ball.get_Body when it exists, else GetComponent(typeof(Rigidbody)) (the v83 way)
static const Method* mBallBody = nullptr;
static void* bodyOf(void* ball) {
    if (!alive(ball)) return nullptr;
    if (mBallBody && fnptr(mBallBody)) { void* b = callObj(mBallBody, ball); if (alive(b)) return b; }
    if (kRbG && mGetComp && fnptr(mGetComp)) { Obj* t = il2cpp_type_get_object(il2cpp_class_get_type(kRbG)); if (t) { void* rb = ((void* (*)(void*, Obj*, const Method*))fnptr(mGetComp))(ball, t, mGetComp); if (alive(rb)) return rb; } }
    return nullptr;
}

// ---------- colours ----------
static Col g_ballCol[64]; static bool g_hasCol[64];
static Col ballColor(int id) {
    if (id >= 0 && id < 64 && g_hasCol[id]) return g_ballCol[id];
    static const Col BC[8] = {{1, 0.85f, 0.1f, 1}, {0.2f, 0.9f, 1, 1}, {1, 0.25f, 0.25f, 1}, {0.7f, 0.5f, 1, 1}, {1, 0.5f, 0.1f, 1}, {0.4f, 1, 0.4f, 1}, {1, 0.2f, 0.9f, 1}, {0.65f, 0.65f, 0.65f, 1}};
    int k = id > 8 ? id - 8 : id; return (k >= 1 && k <= 8) ? BC[k - 1] : WHITE;
}
// the game's ball LIST is in rack-slot order (v83): list index 5 = ball 9, index 9 = ball 8
static std::vector<pp::BallIn> numberedNine(const std::vector<pp::BallIn>& in) {
    static const int NUM[10] = {-1, 1, 2, 3, 4, 9, 5, 6, 7, 8}; std::vector<pp::BallIn> o = in;
    if (g_cfg.nummap) for (auto& b : o) if (b.id >= 1 && b.id <= 9) b.id = NUM[b.id];
    return o;
}

// ---------- the Painter: every shape goes through the line pool ----------
struct Painter {
    void* templ; float y0; size_t used = 0, maxl; float cr;
    struct R { pp::V2 c; double r; }; std::vector<R> rings;
    void* lineAt(float w0 = -1.f, float w1 = -1.f) { void* lr = ensureLine(used, templ); if (lr) forceVisible(lr, w0 > 0.f ? w0 : (float)g_cfg.width, w1); return lr; }
    float yy() const { return y0 + 0.0006f * (float)used; }
    bool poly(const std::vector<pp::V2>& pts, Col c, Col c1, float w, float w1) {
        if (used >= maxl || pts.size() < 2) return true; void* lr = lineAt(w, w1); if (!lr) return false; const float y = yy();
        setCount(lr, (int)pts.size()); for (size_t i = 0; i < pts.size(); ++i) setPos(lr, (int)i, {(float)pts[i].x, y, (float)pts[i].z}); setColor2(lr, c, c1); ++used; return true;
    }
    bool ring(pp::V2 c, double rad, Col col, float w = -1.f) {
        for (auto& d : rings) if (std::fabs(d.c.x - c.x) < 0.0015 && std::fabs(d.c.z - c.z) < 0.0015 && std::fabs(d.r - rad) < 0.001) return true;
        if (used >= maxl) return true; void* lr = lineAt(); if (!lr) return false; rings.push_back({c, rad}); if (w > 0.f) forceVisible(lr, w);
        const int N = 28; setCount(lr, N + 1); const float y = yy(); for (int k = 0; k <= N; ++k) { double a = 6.28318530718 * k / N; setPos(lr, k, {(float)(c.x + rad * std::cos(a)), y, (float)(c.z + rad * std::sin(a))}); }
        setColor(lr, col); ++used; return true;
    }
    bool disc(pp::V2 c, double rad, Col col) {   // solid disc of the TRUE ball size: a thick, tiny closed circle
        if (used >= maxl) return true; void* lr = ensureLine(used, templ); if (!lr) return false; const double rr = rad * 0.25; forceVisible(lr, (float)(2.0 * rad - 2.0 * rr));
        const int N = 12; setCount(lr, N + 1); const float y = yy(); for (int k = 0; k <= N; ++k) { double a = 6.28318530718 * k / N; setPos(lr, k, {(float)(c.x + rr * std::cos(a)), y, (float)(c.z + rr * std::sin(a))}); }
        setColor(lr, col); ++used; return true;
    }
    bool dot(pp::V2 c, Col col) {
        if (used >= maxl) return true; void* lr = ensureLine(used, templ); if (!lr) return false; forceVisible(lr, (float)(cr * 1.3)); const int N = 10; const double rr = cr * 0.22;
        setCount(lr, N + 1); const float y = yy(); for (int k = 0; k <= N; ++k) { double a = 6.28318530718 * k / N; setPos(lr, k, {(float)(c.x + rr * std::cos(a)), y, (float)(c.z + rr * std::sin(a))}); }
        setColor(lr, col); ++used; return true;
    }
};

// ---------- per-frame state shared by the engines ----------
struct BallRec { void* obj; int idx; Vec3 p; float r; };
struct Frame {
    void *th, *cb, *gm, *templ; Vec3 cp; float cr; pp::V2 dir; int nListed = 0;
    std::vector<BallRec> balls;      // every ball on the table EXCEPT the cue ball and pocketed balls (idx = game list index)
    void* cueObj = nullptr; int cueIdx = -1; std::vector<pp::V2> pk;
    bool pulling = false; double fx = 0, fz = 0, F = 0, levelMax = 4.3;
    Vec3 gCue{0, 0, 0}, gTgt{0, 0, 0}; double spinAng = 0;
};
static void why(const char* r, long a = 0) {
    static const char* last = nullptr; static double nxt = 0; double t = nowSec();
    if (r != last || t > nxt) { last = r; nxt = t + 5.0; logFile("[5] draw skipped: %s (%ld)", r, a); }
}

// ================================================================== reading the game state (one aiming frame)
static const Method *mSpinAng = nullptr, *mSpinFactor = nullptr; static int g_offIsHit = off::GM_isHit, g_offFd = off::PB_forceDir, g_offTv = off::PB_targetVel, g_offLv = off::PB_levelMax;
static int g_offAfter = off::CB_afterCollided, g_offTgt = off::CB_targetDir, g_offColl = off::CB_collidedBall; static int g_cueIdx = -1;
static int g_offAim = off::CB_isAiming;   // v92: resolved by NAME at init (a game update can move the hard-coded +0x26D)
static bool aimFallback(void* cb, void* gm) {   // flag unreadable / always 0: the player is aiming when the game's own force vector is non-zero, no shot is running and the power reel (if readable) is pulled
    void* pb = findPB(); if (!pb || rd<bool>(gm, g_offIsHit)) return false;
    const float fx = rd<float>(pb, g_offFd), fz = rd<float>(pb, g_offFd + 8), F = std::sqrt(fx * fx + fz * fz); if (!std::isfinite(F) || F <= 0.02f) return false;
    const float r = readReel(); return r < -900.f || r > 0.02f;
}
static bool readFrame(void* self, Frame& f) {
    f.th = rd<void*>(self, off::CBV_trajectoryHandler); f.cb = rd<void*>(self, off::CBV_cueBall); f.gm = rd<void*>(self, off::CBV_gameManager);
    if (!plausible(f.th) || !plausible(f.cb) || !plausible(f.gm)) { why("CueBallView fields not plausible"); return false; }
    const bool aimFlag = rd<bool>(f.cb, g_offAim);
    if (!aimFlag && !aimFallback(f.cb, f.gm)) {
        static double lastDiag = -9; const double tn = nowSec();
        if (g_cfg.diag && tn - lastDiag > 5.0) { void* pb = findPB(); const float F = pb ? std::sqrt(rd<float>(pb, g_offFd) * rd<float>(pb, g_offFd) + rd<float>(pb, g_offFd + 8) * rd<float>(pb, g_offFd + 8)) : -1.f;
            if (F > 0.02f) { lastDiag = tn; const unsigned char* c = (const unsigned char*)f.cb; char hx[160]; int n = 0; for (int i = 0x258; i < 0x280; ++i) n += snprintf(hx + n, sizeof hx - n, "%02x", c[i]);   // F>0.02 but "not aiming": dump the bytes around the flag so the real offset can be found
                logFile("[5] not aiming although F=%.3f isHit=%d flag@0x%x=%d reel=%.3f CueBall[0x258..0x27f]=%s", F, (int)rd<bool>(f.gm, g_offIsHit), g_offAim, (int)aimFlag, readReel(), hx); } }
        why("not aiming"); return false; }
    f.templ = rd<void*>(f.th, off::TH_targetLine); if (!alive(f.templ)) { why("targetLine template not alive"); return false; }
    return true;
}
static bool readTable(Frame& f) {
    if (!getPos(f.cb, f.cp)) { why("cue ball position unreadable"); return false; }
    f.cr = rd<float>(f.cb, off::B_radius); if (!(f.cr > 0.005f && f.cr < 0.2f)) { why("cue radius implausible", (long)(f.cr * 10000)); return false; }
    // aim direction: scaledDirection (v18 dirsrc=1), then ghost ball, then the cue ray
    Vec3 dS = rd<Vec3>(f.cb, off::CB_scaledDirection), gh = rd<Vec3>(f.cb, off::CB_ghost), dR = rd<Vec3>(f.cb, off::CB_cueRay + 12), dG{gh.x - f.cp.x, 0, gh.z - f.cp.z};
    Vec3 c4[3] = {dS, dG, dR}; Vec3 d{0, 0, 0}; for (auto& c : c4) if (std::fabs(c.x) + std::fabs(c.z) >= 1e-4f) { d = c; break; }
    const double dl = std::sqrt((double)d.x * d.x + (double)d.z * d.z); if (dl < 1e-4) { why("aim direction is zero"); return false; } f.dir = {d.x / dl, d.z / dl};
    void* list = rd<void*>(f.gm, off::GM_balls); f.cueIdx = -1;
    if (plausible(list)) {
        void* items = rd<void*>(list, off::L_items); int n = rd<int>(list, off::L_size); f.nListed = n;
        if (plausible(items) && n > 0 && n < 64) for (int i = 0; i < n; ++i) {
            void* b = rd<void*>(items, off::ARR_data + 8 * i); if (!alive(b)) continue;
            if (b == f.cb) { f.cueIdx = i; continue; }
            if (rd<bool>(b, off::B_isCue) || rd<bool>(b, off::B_insidePocket)) continue;
            Vec3 p; if (!getPos(b, p)) continue; float r = rd<float>(b, off::B_radius); if (!(r > 0.005f && r < 0.2f)) continue;
            f.balls.push_back({b, i, p, r});
            if (i < 64) { Col c = rd<Col>(b, off::B_color); bool ok = c.a > 0.01f && c.r == c.r && c.g == c.g && c.b == c.b && c.r >= 0 && c.r <= 1.5f && c.g >= 0 && c.g <= 1.5f && c.b >= 0 && c.b <= 1.5f && (c.r + c.g + c.b) > 0.02f;
                if (ok) { const float fl = 0.22f; float m = std::max(c.r, std::max(c.g, c.b)); float k = m < fl ? fl / m : 1.f; g_ballCol[i] = {std::min(1.f, c.r * k), std::min(1.f, c.g * k), std::min(1.f, c.b * k), 1.f}; g_hasCol[i] = true; } else g_hasCol[i] = false; }
        }
    }
    f.pk = readPockets(f.gm); if (f.pk.size() < 4) { why("fewer than 4 pockets read", (long)f.pk.size()); return false; }
    // power / pull
    if (void* pb = findPB()) {
        f.levelMax = rd<float>(pb, g_offLv); if (!(f.levelMax > 0.5 && f.levelMax < 20.0)) f.levelMax = 4.3;
        f.fx = rd<float>(pb, g_offFd); f.fz = rd<float>(pb, g_offFd + 8); f.F = std::sqrt(f.fx * f.fx + f.fz * f.fz); if (!std::isfinite(f.F)) f.F = 0;
    }
    { static float lastR = -1.f; static double lastT = -9; const float r = readReel(); const double t = nowSec();
      if (r > 0.02f && r < 1.2f) { lastR = r; lastT = t; f.pulling = true; } else if (lastR > 0.02f && t - lastT < 0.6 && r > -900.f) f.pulling = true;   // held 0.6 s after the stem collapses
      else if (r < -900.f) f.pulling = f.F > 0.05 * f.levelMax; else f.pulling = false; }   // reel unreadable: use the force itself
    f.gCue = rd<Vec3>(f.cb, g_offAfter); f.gTgt = rd<Vec3>(f.cb, g_offTgt);
    if (kSpin && mSpinAng) { void* sc = findOne(kSpin, g_spin, g_spinNext); if (sc) { double a = (double)((float (*)(void*, const Method*))fnptr(mSpinAng))(sc, mSpinAng); f.spinAng = (std::isfinite(a) && std::fabs(a) <= 360.0) ? a : 0; } }
    return true;
}
struct Hasher { uint64_t h = 1469598103934665603ULL; void add(double v, double q = 1000.0) { int64_t k = (int64_t)std::llround(v * q); h = (h ^ (uint64_t)k) * 1099511628211ULL; } };

// ================================================================== FAINT preview engine (v83 core, friction-free long path)
static pp::Table makeTable(const Frame& f) {
    if (g_cfg.usecol && g_meas.ok && g_measFor == f.gm) return pp::Table::fromLimits(f.pk, g_meas.xmin, g_meas.xmax, g_meas.zmin, g_meas.zmax, g_cfg.corner, g_cfg.mid, g_cfg.gapc, g_cfg.gapm);
    return pp::Table::fromPockets(f.pk, g_cfg.inset, g_cfg.corner, g_cfg.mid, g_cfg.gapc, g_cfg.gapm);
}
static std::vector<pp::BallIn> ballsIn(const Frame& f) { std::vector<pp::BallIn> v; for (auto& b : f.balls) v.push_back({b.idx, {b.p.x, b.p.z}, b.r}); return v; }
static pp::Config makeCfg(const Frame& f, bool isBreak) {
    pp::Config C; C.drag = g_cfg.longdrag; C.drag1 = 0.0; C.vMax = std::max(g_cfg.vmax, 4.0); C.cushionKeep = g_cfg.keep; C.ballKeep = g_cfg.ballkeep; C.maxBounces = g_cfg.bounces; C.maxChain = g_cfg.chain; C.budgetMs = 5.0;
    C.bounceThreshold = 0; C.lowBounceKeep = 0; C.stepDt = 0;
    if (isBreak) { C.maxChain = std::max(C.maxChain, 6); C.maxBounces = std::max(C.maxBounces, 5); }
    auto nrm = [](const Vec3& v, pp::V2& o) { double l = std::sqrt((double)v.x * v.x + (double)v.z * v.z); if (!(l > 0.2 && l < 1e3) || v.x != v.x) return false; o = {v.x / l, v.z / l}; return true; };
    pp::V2 oc, ot; if (nrm(f.gTgt, ot) && nrm(f.gCue, oc)) { C.hasOv = true; C.ovTarget = ot; C.ovCue = oc; }   // the game's own first-contact result
    C.spinActive = std::fabs(f.spinAng) > 0.01; C.edgeSpinDeg = f.spinAng; C.edgeSpinBounces = 1;
    return C;
}
static Col trackColor(const pp::Track& t) {
    if (t.id == -1) return WHITE;
    if (g_cfg.colormode == 0) return ballColor(t.id);
    static const Col R[3] = {{0.4f, 1, 0.4f, 1}, {1, 0.85f, 0.1f, 1}, {1, 0.5f, 0.1f, 1}}; return R[std::min(2, std::max(0, t.gen - 1))];
}
static bool drawDraft(Painter& P, const pp::Result& R, float am) {
    std::vector<const pp::Track*> order; for (auto& t : R.tracks) if (t.pts.size() >= 2 && !(t.id == -1 && t.gen == 0)) order.push_back(&t);
    for (auto& t : R.tracks) if (t.pts.size() >= 2 && t.id == -1 && t.gen == 0) order.push_back(&t);   // the cue path last = on top
    const float wv = (float)g_cfg.width;
    for (const pp::Track* tp : order) {
        const pp::Track& t = *tp; if (P.used >= P.maxl) break; const bool isCue = t.id == -1 && t.gen == 0;
        const float wEnd = g_cfg.taper > 0.01 ? wv * (float)(1.0 - std::min(0.8, g_cfg.taper) * (isCue ? 1.0 : 0.7)) : wv;
        Col c = trackColor(t); c.a *= am; Col c1 = c; if (g_cfg.fade > 0.01) c1.a = c.a * (float)(1.0 - std::min(0.9, g_cfg.fade) * (isCue ? 1.0 : 0.6));
        if (!P.poly(t.pts, c, c1, wv, wEnd)) return false;
        if (g_cfg.markers) { if (t.end == pp::END_BALL) { Col rc = isCue ? WHITE : c; rc.a = c.a; if (!P.ring(t.ghost, P.cr, rc)) return false; }
            else if (t.end == pp::END_REST || t.end == pp::END_BOUNCELIMIT) { if (!P.dot(t.pts.back(), c)) return false; } }
    }
    return true;
}

// ================================================================== EXACT engine (PP2 P2Z): the game's own Physics.Simulate
static ppu::UnityHost g_host; static bool g_hostInit = false, g_hostOk = false;
struct LiveState {
    bool have = false; uint64_t sig = 0; double changedAt = 0, lastRun = 0; ppu::Scene scene; std::vector<int> gidx; void* gm = nullptr;
    int fails = 0; bool dead = false; int runs = 0; double lastMs = 0; int lastTicks = 0; bool lastTrunc = false; const char* reason = "-";
};
static LiveState g_live;
static std::string g_liveWhy;
struct RB { bool ok = false, pk = false, cue = false; double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, wx = 0, wy = 0, wz = 0; };
struct SD { float fx = 0, fz = 0, F = 0, tv = 0, sf = -1, sa = 0, gcx = 0, gcz = 0, gtx = 0, gtz = 0, sdx = 0, sdz = 0, r = 0, lvl = 0; };
static struct { bool armed = false; int state = 0, seq = 0, frames = 0; double tStart = 0; std::vector<RB> pre, prev; void *gm = nullptr, *cb = nullptr; int tick0 = 0; SD lastShot; bool haveShot = false; } g_rec;
static int g_recDone = -1; static void* g_recCueRb = nullptr; static int g_recKin = -1, g_recKinN = 0;
static int g_fixTick = 0; static bool g_fixOn = false;   // v92: ServerTracker.FixedUpdate hook (one call = one physics tick, 0.01 s)
static void liveReason(const char* r) { if (g_liveWhy != r) { g_liveWhy = r; if (g_cfg.diag) logFile("[L] exact path not run: %s", r); } }
// P2Z-fix: a client runs Physics.simulationMode = FixedUpdate (only the server instance is Script, P2P) and Unity ignores Physics.Simulate then.  Flip to Script for ONE synchronous
// prediction and restore at once (RAII + fault handler).  Never left changed across frames.
static int g_modeRestore = -1;           // >= 0: the ORIGINAL simulationMode still has to be put back
static int g_modeSwitches = 0, g_modeFails = 0;
// Puts the original mode back and VERIFIES it by reading it again (3 tries). On failure the exact path is disabled and the value stays pending: the per-frame watchdog retries.
static bool restoreSimMode(const char* where) {
    if (g_modeRestore < 0) return true;
    const int want = g_modeRestore; int got = -2;
    for (int i = 0; i < 3 && got != want; ++i) { g_host.setSimMode(want); got = g_host.simMode(); }
    if (got == want) { g_modeRestore = -1; if (g_modeSwitches <= 20 || g_modeSwitches % 10 == 0) logFile("[M] simulationMode after restore=%d (wanted %d) VERIFIED [%s] switches=%d fails=%d", got, want, where, g_modeSwitches, g_modeFails); return true; }
    ++g_modeFails; g_live.dead = true;
    logFile("[X] simulationMode restore FAILED [%s]: wanted %d, reads %d - exact path disabled, watchdog retries every frame (fails=%d)", where, want, got, g_modeFails);
    return false;
}
struct SimModeGuard { ~SimModeGuard() { restoreSimMode("scope exit"); } };
static bool runLive(Frame& f) {
    ppu::UnityHost& H = g_host; H.readConstants();
    const int mode = H.simMode(); SimModeGuard modeGuard;
    if (mode < 0) { liveReason("Physics.simulationMode unreadable"); return false; }
    if (mode != 2 && !H.canSetSimMode()) { liveReason("Physics.simulationMode != Script and set_simulationMode is not available (cannot step physics safely)"); return false; }
    if (rd<bool>(f.gm, g_offIsHit)) { liveReason("GameManager.isHit (a shot is in progress)"); return false; }
    if (f.cueIdx < 0) { liveReason("cue ball not in the ball list"); return false; }
    std::vector<void*> bodies; std::vector<int> gidx; void* cb = bodyOf(f.cb); if (!cb) { liveReason("cue ball has no Rigidbody"); return false; }
    bodies.push_back(cb); gidx.push_back(f.cueIdx);
    for (auto& b : f.balls) { void* rb = bodyOf(b.obj); if (!rb) { liveReason("a ball has no Rigidbody"); return false; } bodies.push_back(rb); gidx.push_back(b.idx); }
    std::string why2; H.ballRadius = f.cr; H.castMask = 0xC00;
    if (kSpin) { int o = fieldOff(kSpin, "spinLayer", -1); void* sc = findOne(kSpin, g_spin, g_spinNext); if (o > 0 && sc) { int m = rd<int>(sc, o); if (m != 0) H.castMask = m; } }
    if (!H.setBalls(bodies, 0, why2)) { liveReason(why2.c_str()); return false; }
    for (int i = 0; i < H.bodyCount(); ++i) if (H.speed(i) > 0.02) { liveReason("balls are still moving"); return false; }
    std::vector<std::pair<double, double>> pk; for (auto& q : f.pk) pk.push_back({q.x, q.z}); H.setPockets(pk, std::max(0.25, 10.0 * f.cr));
    // the game's own shot data (ShotData fields, P2H): direction + force, first-contact prediction, spin factor
    pp2::live::ShotIn S; S.cueId = 0;
    if (g_cfg.livedir == 1) { S.dx = f.dir.x; S.dz = f.dir.z; } else { S.dx = f.fx; S.dz = f.fz; }
    S.F = f.F; S.collisionDir = {f.gCue.x, 0, f.gCue.z}; S.targetDir = {f.gTgt.x, 0, f.gTgt.z};
    if (void* pb = findPB()) S.targetVel = rd<float>(pb, g_offTv);
    void* col = rd<void*>(f.cb, g_offColl); void* sc = kSpin ? findOne(kSpin, g_spin, g_spinNext) : nullptr;
    double sv = -1; if (sc && mSpinFactor) { double v = (double)((float (*)(void*, const Method*))fnptr(mSpinFactor))(sc, mSpinFactor); if (std::isfinite(v) && v >= 0 && v < 100) sv = v; }
    S.spinValue = sv; S.spinUsed = false;
    if (alive(col) && sv >= 0) { void* rb = ppu::callP(H.colRb, col); const int bi = rb ? H.bodyIndex(rb) : -1; S.targetId = bi; S.predEdge = bi < 0 || H.tagIs(col, "Edge"); S.havePred = true; }
    pp2::live::Options O; O.tmax = g_cfg.livet; O.maxMs = g_cfg.livems; O.recEvery = 2; O.moveEvery = 6; O.fastForward = g_cfg.ff != 0;
    if (g_rec.armed) { O.maxMs = 3000.0; O.tmax = 20.0; O.maxTicks = 3000; }   // v92: while the recorder is armed the exact path runs UNBUDGETED (one run up to 3 s) so its final positions are comparable with POST
    if (mode != 2) {
        ++g_modeSwitches; g_modeRestore = mode; H.setSimMode(2); const int now = H.simMode();
        if (g_modeSwitches <= 20 || g_modeSwitches % 10 == 0) logFile("[M] simulationMode before=%d -> set Script, reads %d (switch #%d)", mode, now, g_modeSwitches);
        if (now != 2) { logFile("[X] simulationMode could not be switched to Script (reads %d): prediction skipped", now); restoreSimMode("switch failed"); return false; }
    }
    const double t0 = nowSec(); g_inPredict = g_cfg.gate != 0 ? true : false;
    pp2::live::Out R = pp2::live::predict(H, S, O);
    g_inPredict = false; restoreSimMode("after predict"); const double ms = (nowSec() - t0) * 1000.0;
    if (g_rec.armed && R.ok && !R.frames.empty() && !dirNow().empty()) {   // v92: the last prediction before the shot -> pp2_pred.txt (appended to pp2_shot.txt at END)
        std::string o; char b[300]; const auto& lf = R.frames.back();
        snprintf(b, sizeof b, "PRED run#%d F=%.5f fx=%.5f fz=%.5f spinValue=%.5f t=%.3f ticks=%d trunc=%d contact=%d applied=%d kinSwitched=%d ms=%.0f\n", g_live.runs + 1, S.F, S.dx, S.dz, S.spinValue, R.t, R.ticks, (int)R.truncated, (int)R.firstContact, (int)R.applied, H.kinCount, ms); o += b;
        for (size_t k = 0; k < gidx.size() && k < lf.x.size(); ++k) { snprintf(b, sizeof b, "PREDBALL i=%d alive=%d x=%.5f z=%.5f\n", gidx[k], (int)(k < lf.alive.size() ? lf.alive[k] : 1), lf.x[k], lf.z[k]); o += b; }
        for (auto& e : R.events) { snprintf(b, sizeof b, "PREDEV t=%.3f ball=%d %s x=%.5f z=%.5f\n", e.t, e.ball, e.what.c_str(), e.x, e.z); o += b; }
        if (FILE* pf = fopen((g_dir + "pp2_pred.txt").c_str(), "w")) { fwrite(o.data(), 1, o.size(), pf); fclose(pf); }
    }
    ++g_live.runs; g_live.lastMs = ms; g_live.lastTicks = R.ticks; g_live.lastTrunc = R.truncated;
    if (g_cfg.diag && (g_live.runs <= 6 || g_live.runs % 10 == 0))
        logFile("[L] run#%d ok=%d restored=%d ticks=%d steps=%d %.1fms trunc=%d flag108=%d contact=%d applied=%d F=%.3f dir=(%.3f,%.3f) spinValue=%.3f havePred=%d edge=%d target=%d balls=%d ff=%d mode=%d ffe=%.1e tail=%.1e kin=%d err=%s",
            g_live.runs, (int)R.ok, (int)R.restored, R.ticks, R.steps, ms, (int)R.truncated, (int)R.flag108, (int)R.firstContact, (int)R.applied, S.F, S.dx, S.dz, S.spinValue, (int)S.havePred, (int)S.predEdge, S.targetId, H.bodyCount(), R.ffTicks, R.ffMode, R.ffErr, R.ffTail, H.kinCount, R.err.c_str());
    if (!R.restored) { logFile("[X] live predictor could not restore every ball - exact path disabled for this session"); g_live.dead = true; }
    if (!R.ok) { if (++g_live.fails >= 3) { g_live.dead = true; logFile("[X] exact path failed 3 times (%s) - disabled for this session, the faint preview keeps working", R.err.c_str()); } return false; }
    g_live.fails = 0; g_live.scene = ppu::buildScene(R); g_live.gidx = gidx; g_live.gm = f.gm; g_live.have = true; g_liveWhy.clear();
    return true;
}
// returns true when an exact scene for the CURRENT input exists
static bool liveStep(Frame& f) {
    if (!g_hostOk || g_cfg.live == 0 || g_live.dead) { g_live.have = false; return false; }
    if (!f.pulling || f.F < 0.02) { g_live.have = false; g_live.sig = 0; return false; }
    Hasher h; h.add(f.F); h.add(f.fx); h.add(f.fz); h.add(f.cp.x); h.add(f.cp.z); h.add(f.dir.x); h.add(f.dir.z); h.add(f.gCue.x, 400); h.add(f.gCue.z, 400); h.add(f.gTgt.x, 400); h.add(f.gTgt.z, 400); h.add(f.spinAng);
    for (auto& b : f.balls) { h.add(b.idx); h.add(b.p.x); h.add(b.p.z); }
    const double now = nowSec();
    if (h.h != g_live.sig || g_live.gm != f.gm) { g_live.sig = h.h; g_live.changedAt = now; g_live.have = false; g_live.gm = f.gm; }
    if (g_live.have) return true;
    const double needStill = g_rec.armed ? std::max(g_cfg.settle, 0.8) : g_cfg.settle;   // v92c: while recording, the unbudgeted (~1 s) run only starts when the aim has been still for 0.8 s (it froze the game on every small aim change)
    if (now - g_live.changedAt < needStill || now - g_live.lastRun < (g_rec.armed ? std::max(g_cfg.gap, 1.5) : g_cfg.gap)) return false;
    g_live.lastRun = now; return runLive(f);
}
static bool drawLive(Painter& P, const Frame& f) {
    const ppu::Scene& S = g_live.scene; const float wv = (float)g_cfg.width;
    auto gi = [&](int hostIdx) { return hostIdx >= 0 && hostIdx < (int)g_live.gidx.size() ? g_live.gidx[hostIdx] : -1; };
    auto radOf = [&](int hostIdx) -> double { const int g = gi(hostIdx); if (hostIdx == 0) return f.cr; for (auto& b : f.balls) if (b.idx == g) return b.r; return f.cr; };
    std::vector<const ppu::Path*> order; for (auto& p : S.paths) if (p.ball != 0) order.push_back(&p); for (auto& p : S.paths) if (p.ball == 0) order.push_back(&p);   // cue last
    for (const ppu::Path* p : order) {
        const bool cue = p->ball == 0; Col c = cue ? WHITE : (g_cfg.colormode == 0 ? ballColor(gi(p->ball)) : Col{1, 0.85f, 0.1f, 1}); c.a = 1.f;
        std::vector<pp::V2> pts; for (auto& q : p->pts) pts.push_back({q.x, q.z});
        if (!P.poly(pts, c, c, wv, wv * (cue ? 0.8f : 0.7f))) return false;
        if (!p->pocketed && g_cfg.ballrings) { Col dc = c; dc.a = 0.85f; if (!P.disc(pts.back(), radOf(p->ball), dc)) return false; }   // where the ball STANDS when everything stopped
    }
    for (auto& m : S.marks) {
        if (m.kind == 0 && g_cfg.markers) { if (!P.ring({m.p.x, m.p.z}, f.cr, WHITE)) return false; }                   // ghost ball: the cue ball at its first contact
        else if (m.kind == 1 && g_cfg.rings) { int bi = -1; double bd = 1e9; for (size_t i = 0; i < f.pk.size(); ++i) { double d = std::hypot(f.pk[i].x - m.p.x, f.pk[i].z - m.p.z); if (d < bd) { bd = d; bi = (int)i; } }
            if (bi >= 0 && !P.ring(f.pk[bi], std::max(0.06, 2.4 * (double)f.cr), Col{1.f, 0.02f, 0.06f, 1.f}, wv * 1.2f)) return false; }   // the pocket a ball drops into
    }
    return true;
}

// ================================================================== 9-ball suggestion + break (v81/v82 planner; approximate engine, drawn in green: it is a SUGGESTION)
static bool drawSuggest(Painter& P, const Frame& f, const pp::Table& T, const pp::Config& C) {
    static pp::Planner s_plan; static uint64_t s_sig = 0; static bool s_have = false, s_done = false; static void* s_gm = nullptr; static pp::Result s_R;
    static pp::BreakPlanner b_plan; static uint64_t b_sig = 0; static bool b_have = false, b_done = false; static void* b_gm = nullptr; static pp::Result b_R;
    const int nb = (int)f.balls.size(); const bool nine = g_cfg.suggest && f.nListed == 10 && nb >= 1 && nb < 9, brk = g_cfg.suggest && f.nListed == 10 && nb == 9;
    const float w2 = (float)g_cfg.width * 1.1f; const Col green{0.25f, 1.f, 0.45f, 0.95f};
    const std::vector<pp::BallIn> bi = ballsIn(f);
    if (nine) {
        Hasher h; h.add(f.cp.x, 500); h.add(f.cp.z, 500); h.add(g_cfg.bounces); for (auto& b : f.balls) { h.add(b.idx); h.add(b.p.x, 500); h.add(b.p.z, 500); }
        if (!s_have || h.h != s_sig || s_gm != f.gm) { s_have = true; s_sig = h.h; s_gm = f.gm; s_done = false; s_plan.begin(T, C, numberedNine(bi), {f.cp.x, f.cp.z}, f.cr); }
        if (!s_done && s_plan.step(1.2)) { s_done = true; if (s_plan.best().ok) s_R = s_plan.replay();
            if (g_cfg.diag) logFile("[27] suggest: target=%d candidates=%d ok=%d kind=%d pocket=%d power=%.2f potted=%d robust=%d", s_plan.target(), s_plan.candidates(), (int)s_plan.best().ok, s_plan.best().kind, s_plan.best().pocket, s_plan.best().power, s_plan.best().potted, s_plan.best().robust); }
        const pp::Suggestion& sg = s_plan.best();
        if (s_done && sg.ok) {
            for (auto& t : s_R.tracks) { if (t.pts.size() < 2 || t.gen > 2 || (t.id == -1 && t.gen >= 1) || P.used >= P.maxl) continue;
                Col sc = t.id == -1 ? green : (t.id == sg.target ? Col{1.f, 0.85f, 0.2f, 0.95f} : Col{0.45f, 0.8f, 1.f, 0.9f}); if (!P.poly(t.pts, sc, sc, w2, w2)) return false; }
            if (sg.pocket >= 0 && sg.pocket < (int)T.pockets.size() && !P.ring(T.pockets[sg.pocket].p, T.pockets[sg.pocket].mouth, green, (float)g_cfg.width * 1.4f)) return false;
        }
    } else { s_have = false; }
    if (brk) {
        Hasher h; for (auto& b : f.balls) { h.add(b.p.x, 500); h.add(b.p.z, 500); }   // the rack only: the cue ball moves while you place it
        if (!b_have || h.h != b_sig || b_gm != f.gm) { b_have = true; b_sig = h.h; b_gm = f.gm; b_done = false; b_plan.begin(T, C, bi, {f.cp.x, f.cp.z}, f.cr); }
        if (!b_done && b_plan.step(2.0)) { b_done = true; if (b_plan.best().ok) b_R = b_plan.replay(); if (g_cfg.diag) logFile("[29] break: candidates=%d ok=%d", b_plan.candidates(), (int)b_plan.best().ok); }
        const pp::BreakSuggestion& bs = b_plan.best();
        if (b_done && bs.ok) {
            for (auto& t : b_R.tracks) { if (t.pts.size() < 2 || t.gen > 2 || (t.id == -1 && t.gen >= 1) || P.used >= P.maxl) continue;
                Col sc = t.id == -1 ? green : Col{0.45f, 0.8f, 1.f, 0.9f}; if (!P.poly(t.pts, sc, sc, w2, w2)) return false; }
            if (!P.ring(bs.spot, f.cr * 1.5, green, (float)g_cfg.width * 1.6f)) return false;   // put the cue ball inside this ring
        }
    } else { b_have = false; }
    return true;
}

// ================================================================== the per-frame work
static int g_perfN = 0; static double g_perfEma = 0, g_perfMax = 0, g_perfLog = 0;
struct DrawTimer { double t0; DrawTimer() : t0(nowSec()) {}
    ~DrawTimer() { const double t = nowSec(), ms = (t - t0) * 1000.0; g_perfEma = g_perfEma <= 0 ? ms : g_perfEma * 0.92 + ms * 0.08; g_perfMax = std::max(g_perfMax * 0.995, ms); ++g_perfN;
        if (g_cfg.diag && t - g_perfLog > 6.0) { g_perfLog = t; logFile("[P] perf: draw ema=%.2fms max=%.2fms | exact runs=%d last=%.1fms ticks=%d trunc=%d dead=%d | %s", g_perfEma, g_perfMax, g_live.runs, g_live.lastMs, g_live.lastTicks, (int)g_live.lastTrunc, (int)g_live.dead, g_hostOk ? "host ok" : "host NOT ready"); } } };

static void draw(void* self) {
    g_dstage = 91; DrawTimer drawTimer; Frame f;
    if (!g_cfg.on) { hideAll(); return; }
    if (!readFrame(self, f)) { hideAll(); return; }
    g_dstage = 92;
    if (g_poolOwner != f.gm || g_template != f.templ) { releasePool(); g_poolOwner = f.gm; g_template = f.templ; g_lrKlass = rd<void*>(f.templ, 0); g_drawAfter = nowSec() + 1.0; g_pb = nullptr; g_measFor = nullptr; g_measTries = 0; g_live = LiveState(); logFile("[4] new match/template: pool reset; drawing resumes in 1s"); }
    if (nowSec() < g_drawAfter) return;
    g_dstage = 93; if (!g_skipDrain) drainPending();
    g_dstage = 94; if (!readTable(f)) { hideAll(); return; }
    measure(f.gm, f.pk, f.cr);
    if (g_cfg.diag) { static double nx = 0; if (nowSec() > nx) { nx = nowSec() + 3.0; logFile("[10] aim: pulling=%d F=%.3f levelMax=%.2f fd=(%.3f,%.3f) scaled=(%.3f,%.3f) balls=%d listed=%d pockets=%d cr=%.4f gameFirstContact: cue=(%.2f,%.2f) tgt=(%.2f,%.2f) spinAng=%.2f isHit=%d",
        (int)f.pulling, f.F, f.levelMax, f.fx, f.fz, f.dir.x, f.dir.z, (int)f.balls.size(), f.nListed, (int)f.pk.size(), (double)f.cr, (double)f.gCue.x, (double)f.gCue.z, (double)f.gTgt.x, (double)f.gTgt.z, f.spinAng, (int)rd<bool>(f.gm, g_offIsHit)); } }
    g_dstage = 5;
    Painter P; P.templ = f.templ; P.y0 = f.cp.y + (float)g_cfg.lift; P.cr = f.cr; const bool isBreak = f.balls.size() >= 14; P.maxl = (size_t)std::max(isBreak ? 120.0 : 1.0, std::min(150.0, g_cfg.maxlines));
    // 1) the exact path (only while the cue is pulled; it appears once the input has been still for `settle` s)
    g_dstage = 6; const bool exact = liveStep(f); g_dstage = 9;
    // 2) the faint preview: while there is no exact path for the current input (and the player did not ask for exact-only)
    const pp::Table T = makeTable(f); const pp::Config C = makeCfg(f, isBreak);
    if (exact) { if (!drawLive(P, f)) { hideAll(); return; } }
    else if (g_cfg.draft && g_cfg.live != 2) {
        g_dstage = 7; static pp::Result R; static bool have = false; static uint64_t sig = 0; static double tR = 0; static void* gmR = nullptr;
        Hasher h; h.add(f.dir.x, 20000); h.add(f.dir.z, 20000); h.add(f.cp.x, 20000); h.add(f.cp.z, 20000); h.add(f.spinAng); h.add(C.hasOv ? 1.0 : 0.0); h.add(f.gCue.x, 400); h.add(f.gCue.z, 400); h.add(f.gTgt.x, 400); h.add(f.gTgt.z, 400);
        h.add((double)f.balls.size()); for (auto& b : f.balls) { h.add(b.p.x, 20000); h.add(b.p.z, 20000); } h.add(g_meas.ok ? 1.0 : 0.0); h.add(g_cfg.bounces); h.add(g_cfg.chain);
        const double tn = nowSec();
        if (!(have && gmR == f.gm && (h.h == sig || tn - tR < 0.033))) { pp::Sim sim(T, C, ballsIn(f), {f.cp.x, f.cp.z}, f.cr); R = sim.run(f.dir, 1.0); have = true; tR = tn; sig = h.h; gmR = f.gm; }
        if (!drawDraft(P, R, f.pulling ? (float)g_cfg.longalpha : 1.f)) { hideAll(); return; }
    }
    // 3) suggestion (approximate engine) - lowest priority for the line budget except the rings
    g_dstage = 8; if (!drawSuggest(P, f, T, C)) { hideAll(); return; }
    // 4) a thin ring on every ball where it stands now (cue ball included)
    if (g_cfg.ballrings) {
        Col cw = WHITE; cw.a = 0.8f; if (!P.ring({f.cp.x, f.cp.z}, (double)f.cr, cw, (float)(g_cfg.width * g_cfg.ringw))) { hideAll(); return; }
        for (auto& b : f.balls) { Col c2 = ballColor(b.idx); c2.a = 0.7f; if (!P.ring({b.p.x, b.p.z}, b.r, c2, (float)(g_cfg.width * g_cfg.ringw))) { hideAll(); return; } }
    }
    for (size_t i = P.used; i < g_pool.size(); ++i) if (validLine(g_pool[i].lr)) setCount(g_pool[i].lr, 0);
}

// ================================================================== hook, gates, start-up
static void (*g_orig)(void*, const Method*) = nullptr; static bool g_dead = false; static double g_coolUntil = 0, g_faultT[8] = {0}; static int g_faultN = 0, g_err = 0;
static void resetAll() {
    for (auto& l : g_pool) g_pending.push_back(l);
    shClear(); g_pool.clear(); g_poolOwner = nullptr; g_template = nullptr; g_pb = nullptr; g_pk.clear(); g_pkFor = nullptr; g_meas = Meas(); g_measFor = nullptr; g_measTries = 0; g_live.have = false;
}
// v87: PP2 name probe -> pp2_probe.txt (dry: reflection lookups only).  Called from the init thread (once per launch) and from the hook when the app asks (cfg probe=<new number>).
static int g_probeDone = -1;
static void runProbe(const char* why) {
    int ok = 0, miss = 0; std::string rep = ppu::probePP2(ok, miss);
    if (!dirNow().empty()) { FILE* f = fopen((g_dir + "pp2_probe.txt").c_str(), "w"); if (f) { fprintf(f, "PP2 probe (%s) pid=%d uid=%d\n%s", why, (int)getpid(), (int)getuid(), rep.c_str()); fclose(f); } }
    logFile("[P2] probe (%s): %d OK, %d MISSING -> pp2_probe.txt", why, ok, miss);
}
// ================================================================== v87: ONE-SHOT RECORDER -> pp2_shot.txt
// Armed by the app (cfg rec=<new number>).  Waits for the balls to be at rest (isHit=false) and keeps the latest rest snapshot + the game's shot data; when isHit turns true it writes
// PRE (all balls) + SHOT (force / direction / spin / the game's own first-contact prediction) and then one line per rendered frame with every moving ball (x z vx vz of the Rigidbody);
// when isHit turns false again it writes POST (all balls) and disarms.  Read-only: it never writes to the game.  Frames are LateUpdate samples (not every physics step).
static void recWrite(bool truncate, const std::string& s) {
    if (dirNow().empty()) return; FILE* f = fopen((g_dir + "pp2_shot.txt").c_str(), truncate ? "w" : "a"); if (!f) return; fwrite(s.data(), 1, s.size(), f); fclose(f);
}
static bool recSnap(void* gm, void* cb, std::vector<RB>& out) {
    out.clear(); void* list = rd<void*>(gm, off::GM_balls); if (!plausible(list)) return false;
    void* items = rd<void*>(list, off::L_items); const int n = rd<int>(list, off::L_size); if (!plausible(items) || n <= 0 || n >= 64) return false;
    std::vector<void*> bodies; std::vector<int> map; out.resize(n); int cueK = -1;
    for (int i = 0; i < n; ++i) {
        void* b = rd<void*>(items, off::ARR_data + 8 * i); if (!alive(b)) continue; out[i].cue = (b == cb); out[i].pk = rd<bool>(b, off::B_insidePocket);
        void* rb = bodyOf(b); if (!rb) continue; if (b == cb) { cueK = (int)bodies.size(); g_recCueRb = rb; } bodies.push_back(rb); map.push_back(i);
    }
    if (bodies.empty()) return false; if (g_host.rbGetKin.ok()) { int kn = 0; for (void* rb : bodies) kn += ppu::callB(g_host.rbGetKin, rb) == 1; g_recKin = kn; g_recKinN = (int)bodies.size(); }
    std::string w; if (!g_host.setBalls(bodies, cueK < 0 ? 0 : cueK, w)) return false;
    for (size_t k = 0; k < map.size(); ++k) { pp2::live::BallState s; if (!g_host.get((int)k, s)) continue; RB& o = out[map[k]]; o.ok = true; o.x = s.pos.x; o.y = s.pos.y; o.z = s.pos.z; o.vx = s.vel.x; o.vy = s.vel.y; o.vz = s.vel.z; o.wx = s.ang.x; o.wy = s.ang.y; o.wz = s.ang.z; }
    return true;
}
static SD recShot(void* cb) {
    SD d; if (void* pb = findPB()) { d.fx = rd<float>(pb, g_offFd); d.fz = rd<float>(pb, g_offFd + 8); d.tv = rd<float>(pb, g_offTv); d.lvl = rd<float>(pb, g_offLv); }
    d.F = std::sqrt(d.fx * d.fx + d.fz * d.fz); if (!std::isfinite(d.F)) d.F = 0;
    Vec3 a = rd<Vec3>(cb, g_offAfter), t = rd<Vec3>(cb, g_offTgt), sdv = rd<Vec3>(cb, off::CB_scaledDirection); d.gcx = a.x; d.gcz = a.z; d.gtx = t.x; d.gtz = t.z; d.sdx = sdv.x; d.sdz = sdv.z; d.r = rd<float>(cb, off::B_radius);
    if (kSpin) { void* sc = findOne(kSpin, g_spin, g_spinNext); if (sc) {
        if (mSpinFactor) { double v = (double)((float (*)(void*, const Method*))fnptr(mSpinFactor))(sc, mSpinFactor); if (std::isfinite(v) && v >= 0 && v < 100) d.sf = (float)v; }
        if (mSpinAng) { double v = (double)((float (*)(void*, const Method*))fnptr(mSpinAng))(sc, mSpinAng); if (std::isfinite(v) && std::fabs(v) <= 360.0) d.sa = (float)v; } } }
    return d;
}
static void recBalls(std::string& o, const char* tag, const std::vector<RB>& v) {
    char b[320]; for (size_t i = 0; i < v.size(); ++i) { if (!v[i].ok) { snprintf(b, sizeof b, "%s i=%d unreadable\n", tag, (int)i); o += b; continue; }
        snprintf(b, sizeof b, "%s i=%d cue=%d pk=%d x=%.5f y=%.5f z=%.5f vx=%.5f vy=%.5f vz=%.5f wx=%.4f wy=%.4f wz=%.4f\n", tag, (int)i, (int)v[i].cue, (int)v[i].pk, v[i].x, v[i].y, v[i].z, v[i].vx, v[i].vy, v[i].vz, v[i].wx, v[i].wy, v[i].wz); o += b; }
}
static const Method *g_mFixedT = nullptr, *g_mFixedDt = nullptr, *g_mVel2 = nullptr, *g_mKin = nullptr; static bool g_recRes = false;
static void recResolve() {
    if (g_recRes) return; g_recRes = true;
    if (Klass* kt = findClass("UnityEngine", "Time")) { g_mFixedT = findMethod(kt, "get_fixedTime", 0); g_mFixedDt = findMethod(kt, "get_fixedDeltaTime", 0); if (g_mFixedT && !fnptr(g_mFixedT)) g_mFixedT = nullptr; if (g_mFixedDt && !fnptr(g_mFixedDt)) g_mFixedDt = nullptr; }
    if (g_host.kRb) { g_mVel2 = findMethod(g_host.kRb, "get_velocity", 0); g_mKin = findMethod(g_host.kRb, "get_isKinematic", 0); if (g_mVel2 && !fnptr(g_mVel2)) g_mVel2 = nullptr; if (g_mKin && !fnptr(g_mKin)) g_mKin = nullptr; }
    logFile("[R] resolved: Time.fixedTime=%d fixedDeltaTime=%d Rigidbody.velocity=%d isKinematic=%d", g_mFixedT != nullptr, g_mFixedDt != nullptr, g_mVel2 != nullptr, g_mKin != nullptr);
}
static double recFixedT() { return g_mFixedT ? (double)((float (*)(const Method*))fnptr(g_mFixedT))(g_mFixedT) : -1.0; }
static void recTick(void* self, bool fromFixed) {
    if (!g_rec.armed) return;
    if (!g_hostOk) { logFile("[R] recorder needs the exact-path host (not ready) - disarmed"); g_rec.armed = false; return; }
    recResolve();
    void *gm, *cb; if (fromFixed) { gm = g_rec.gm; cb = g_rec.cb; } else { gm = rd<void*>(self, off::CBV_gameManager); cb = rd<void*>(self, off::CBV_cueBall); g_rec.gm = gm; g_rec.cb = cb; }
    if (!plausible(gm) || !plausible(cb)) return;
    const bool hit = rd<bool>(gm, g_offIsHit); const double t = nowSec(); std::vector<RB> cur; char b[400];
    if (g_rec.state == 0) {
        if (!hit) { if (fromFixed) return; if (recSnap(gm, cb, cur)) g_rec.pre = cur; const SD d = recShot(cb); if (d.F > 0.02f) { g_rec.lastShot = d; g_rec.haveShot = true; } return; }
        if (g_rec.pre.empty()) return;   // armed in the middle of a shot: wait for the next rest state
        const SD d = g_rec.haveShot ? g_rec.lastShot : recShot(cb); std::string o;
        snprintf(b, sizeof b, "# PP2 shot record #%d  radius=%.5f\n", g_rec.seq + 1, (double)d.r); o += b;
        o += "POCKETS"; { const auto pk = readPockets(gm); for (auto& q : pk) { snprintf(b, sizeof b, " (%.4f,%.4f)", q.x, q.z); o += b; } } o += "\n";
        recBalls(o, "PRE", g_rec.pre);
        snprintf(b, sizeof b, "SHOT fx=%.5f fz=%.5f F=%.5f targetVel=%.5f levelMax=%.3f spinFactor=%.5f spinAngDeg=%.3f gameCueDir=(%.5f,%.5f) gameTargetDir=(%.5f,%.5f) scaledDir=(%.5f,%.5f) fromIdle=%d\n",
            d.fx, d.fz, d.F, d.tv, d.lvl, d.sf, d.sa, d.gcx, d.gcz, d.gtx, d.gtz, d.sdx, d.sdz, (int)g_rec.haveShot); o += b;
        snprintf(b, sizeof b, "SIM simulationMode=%d (2=Script, 0=FixedUpdate) contactOffset=%.5f fixedDeltaTime=%.7f isKinematic(cue)=%d kinematicAtLastIdleSnapshot=%d/%d\n", g_host.simMode(), g_host.contactOffset, g_mFixedDt ? (double)((float (*)(const Method*))fnptr(g_mFixedDt))(g_mFixedDt) : -1.0, (g_mKin && g_recCueRb) ? (int)((bool (*)(void*, const Method*))fnptr(g_mKin))(g_recCueRb, g_mKin) : -1, g_recKin, g_recKinN); o += b;
        o += "FRAME k=<physics tick since isHit (0.01 s each; -1 = rendered-frame sample)> t=<wall s since isHit> T=<Time.fixedTime = physics clock> cv2=<Rigidbody.velocity of the cue ball> then  i:x,z,vx,vz  for every ball that moved (>0.01mm since the last frame) or has speed>0.0005 m/s\n"; recWrite(true, o);
        g_rec.state = 1; g_rec.tStart = t; g_rec.frames = 0; g_rec.tick0 = g_fixTick; logFile("[R] shot started: F=%.3f dir=(%.3f,%.3f) spinFactor=%.3f", d.F, d.fx, d.fz, d.sf);
    }
    if ((fromFixed || !g_fixOn) && recSnap(gm, cb, cur)) {   // per physics tick when the FixedUpdate hook is installed, else per rendered frame
        if (g_rec.frames < 9000) {
            std::string o; snprintf(b, sizeof b, "F k=%d t=%.4f T=%.5f", fromFixed ? g_fixTick - g_rec.tick0 : -1, t - g_rec.tStart, recFixedT()); o += b;
            if (g_mVel2 && g_recCueRb) { struct V3 { float x, y, z; }; V3 v = ((V3 (*)(void*, const Method*))fnptr(g_mVel2))(g_recCueRb, g_mVel2); snprintf(b, sizeof b, " cv2=%.5f,%.5f", v.x, v.z); o += b; }
            for (size_t i = 0; i < cur.size(); ++i) { if (!cur[i].ok) continue;
                const RB* pv = i < g_rec.prev.size() && g_rec.prev[i].ok ? &g_rec.prev[i] : (i < g_rec.pre.size() && g_rec.pre[i].ok ? &g_rec.pre[i] : nullptr);
                const double moved = pv ? std::sqrt((cur[i].x - pv->x) * (cur[i].x - pv->x) + (cur[i].z - pv->z) * (cur[i].z - pv->z)) : 1.0;
                if (moved > 1e-5 || std::sqrt(cur[i].vx * cur[i].vx + cur[i].vz * cur[i].vz) > 0.0005) { snprintf(b, sizeof b, " %d:%.5f,%.5f,%.5f,%.5f", (int)i, cur[i].x, cur[i].z, cur[i].vx, cur[i].vz); o += b; } }
            g_rec.prev = cur;
            o += "\n"; recWrite(false, o); ++g_rec.frames;
        }
    }
    if (!hit || t - g_rec.tStart > 45.0) {
        std::string o; snprintf(b, sizeof b, "END t=%.3f frames=%d reason=%s\n", t - g_rec.tStart, g_rec.frames, hit ? "timeout" : "isHit=false"); o += b;
        if (cur.empty()) recSnap(gm, cb, cur); recBalls(o, "POST", cur);
        if (FILE* pf = fopen((g_dir + "pp2_pred.txt").c_str(), "r")) { char buf[4096]; size_t n; o += "--- last exact-path prediction before the shot (pp2_pred.txt) ---\n"; while ((n = fread(buf, 1, sizeof buf, pf)) > 0 && o.size() < 60000) o.append(buf, n); fclose(pf); } else o += "--- no exact-path prediction was made before the shot ---\n";
        recWrite(false, o);
        ++g_rec.seq; g_rec.armed = false; g_rec.state = 0; g_rec.pre.clear(); g_rec.haveShot = false; logFile("[R] shot recorded: %.2fs, %d frames -> pp2_shot.txt", t - g_rec.tStart, g_rec.frames);
    }
}
static void (*g_origFix)(void*, const Method*) = nullptr;
static void hookedFix(void* self, const Method* mi) {
    ++g_fixTick; g_origFix(self, mi);
    if (g_rec.armed && !g_dead) { g_tid = tidNow(); g_in = 1; if (sigsetjmp(g_jb, 1) == 0) recTick(nullptr, true); else { logFile("[X] fault inside the shot recorder (FixedUpdate) - disarmed"); g_rec.armed = false; } g_in = 0; }
}
static void hooked(void* self, const Method* mi) {
    g_orig(self, mi);
    { static bool first = true; if (first) { first = false; logFile("[5] hook is being called by the game (LateUpdate running)"); } }
    static double nextCfg = 0, nextH = 0; const double t = nowSec();
    if (t > nextCfg) { nextCfg = t + 1.0; loadCfg();
        if (g_recDone < 0) g_recDone = g_cfg.rec;
        else if (g_cfg.rec > 0 && g_cfg.rec != g_recDone) { g_recDone = g_cfg.rec; g_rec = decltype(g_rec)(); g_rec.armed = true; logFile("[R] recorder armed: waiting for a shot (balls at rest, then isHit)"); }
        if (g_probeDone < 0) g_probeDone = g_cfg.probe;   // a stale number from an earlier session does not re-run
        else if (g_cfg.probe > 0 && g_cfg.probe != g_probeDone) { g_probeDone = g_cfg.probe; g_tid = tidNow(); g_in = 1; if (sigsetjmp(g_jb, 1) == 0) runProbe("button"); else logFile("[X] fault inside the PP2 probe"); g_in = 0; } }
    if (t > nextH) { nextH = t + 2.0; ensureHandler(); }
    if (g_rec.armed) { g_tid = tidNow(); g_in = 1; if (sigsetjmp(g_jb, 1) == 0) recTick(self, false); else { logFile("[X] fault inside the shot recorder - disarmed"); g_rec.armed = false; } g_in = 0; }
    if (g_modeRestore >= 0) { g_tid = tidNow(); g_in = 1; if (sigsetjmp(g_jb, 1) == 0) restoreSimMode("watchdog"); g_in = 0; }   // a pending restore is retried before anything else, even when the feature is disabled
    if (g_dead || t < g_coolUntil) return;
    g_tid = tidNow(); g_in = 1;
    if (sigsetjmp(g_jb, 1) == 0) { g_dstage = 99; draw(self); }
    else {
        g_in = 0; ++g_err; g_inPredict = false;
        // a fault inside the game's Physics.Simulate leaves the balls where the simulation was: put them back FIRST (guarded again)
        // the mode goes back FIRST and in its own guarded block (a single setter, least likely to fault again), then the balls; nothing here runs inside the signal handler itself
        g_in = 1; if (sigsetjmp(g_jb, 1) == 0) restoreSimMode("fault recovery"); else logFile("[X] simulationMode restore faulted - the watchdog retries next frame"); g_in = 0;
        g_in = 1; if (sigsetjmp(g_jb, 1) == 0) { if (!pp2::live::emergencyRestore()) logFile("[X] emergency restore reported a failed write"); } else logFile("[X] emergency restore itself faulted"); g_in = 0;
        if (g_dstage >= 6 && g_dstage <= 6) { g_live.dead = true; logFile("[X] fault while the exact predictor was running (stage %d): exact path disabled for this session", (int)g_dstage); }
        resetAll(); g_coolUntil = nowSec() + 2.0;
        g_faultT[g_faultN++ & 7] = nowSec(); int recent = 0; for (int i = 0; i < 8; ++i) if (g_faultT[i] > nowSec() - 120.0) ++recent;
        logFile("fault inside draw() at stage %d addr=%p pc=%p (libil2cpp+0x%lx): caches reset, retry in 2s (faults in last 2min: %d)", (int)g_dstage, (void*)g_fAddr, (void*)g_fPc, (unsigned long)(g_fPc - g_il2Base), recent);
        if (recent >= 5) { g_dead = true; logFile("too many faults: feature disabled until the game restarts"); }
    }
    g_in = 0;
}

// ---- gates: the game's collision / trigger callbacks do nothing while OUR simulation runs (one typed trampoline per hooked method, 1 reference-type argument each) ----
static const int MAXG = 32; static void* g_gateOrig[MAXG]; static int g_gateN = 0;
template <int N> static void gateFn(void* self, void* arg, const Method* m) { if (g_inPredict) return; ((void (*)(void*, void*, const Method*))g_gateOrig[N])(self, arg, m); }
template <int... I> static void fillGates(void** t, std::integer_sequence<int, I...>) { ((t[I] = (void*)&gateFn<I>), ...); }
static void installGates() {
    if (!g_cfg.gate) { logFile("[G] gates off (cfg gate=0)"); return; }
    if (!il2cpp_image_get_class_count || !il2cpp_image_get_class || !il2cpp_class_get_methods || !il2cpp_method_get_name) { logFile("[G] reflection exports missing: no gates"); return; }
    static void* tbl[MAXG]; fillGates(tbl, std::make_integer_sequence<int, MAXG>());
    static const char* names[] = {"OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit"};
    size_t na = 0; Assembly** as = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &na); std::string list;
    for (size_t a = 0; as && a < na && g_gateN < MAXG; ++a) {
        const Image* im = il2cpp_assembly_get_image(as[a]); const char* in = il2cpp_image_get_name(im); if (!in || !strstr(in, "Assembly-CSharp") || strstr(in, "firstpass")) continue;
        const size_t nc = il2cpp_image_get_class_count(im);
        for (size_t c = 0; c < nc && g_gateN < MAXG; ++c) {
            Klass* k = (Klass*)il2cpp_image_get_class(im, c); if (!k) continue; void* it = nullptr;
            while (const Method* m = il2cpp_class_get_methods(k, &it)) {
                if (g_gateN >= MAXG) break; const char* mn = il2cpp_method_get_name(m); if (!mn || il2cpp_method_get_param_count(m) != 1 || il2cpp_method_is_generic(m) || !fnptr(m)) continue;
                bool hit = false; for (const char* n : names) if (!strcmp(mn, n)) { hit = true; break; } if (!hit) continue;
                const int k2 = g_gateN++; g_gateOrig[k2] = fnptr(m); __atomic_store_n(&fnptr(m), tbl[k2], __ATOMIC_SEQ_CST);
                const char* cn = il2cpp_class_get_name ? il2cpp_class_get_name(k) : "?"; list += std::string(cn ? cn : "?") + "." + mn + " ";
            }
        }
    }
    logFile("[G] gated %d game callbacks: %s", g_gateN, list.c_str());
}

static bool resolve() {
    Klass *kObj = findClass("UnityEngine", "Object"), *kComp = findClass("UnityEngine", "Component"), *kTr = findClass("UnityEngine", "Transform"), *kLR = findClass("UnityEngine", "LineRenderer");
    auto N = [&](const char* ns, const char* nm) { Klass* k = findClass(ns, nm); if (!k) k = findClass("", nm); logFile("[4] %s %s", nm, k ? "found" : "NOT found"); return k; };
    kPhysicsBuilder = N("PoolLiveTourMax.Physic.Handlers", "PhysicsBuilder"); kGameIn = N("PoolLiveTourMax.Inputs", "GameInpusHandler"); kSpin = N("PoolLiveTourMax.Gameplay.Shot", "SpinController");
    kPocket = N("PoolLiveTourMax.GameRules", "Pocket"); kBall = N("PoolLiveTourMax.Gameplay.Balls", "Ball"); kCueBall = N("PoolLiveTourMax.Gameplay.Balls", "CueBall"); kGM = N("PoolLiveTourMax.Managers", "GameManager");
    kRbG = findClass("UnityEngine", "Rigidbody");
    if (kComp) { mGetGO = findMethod(kComp, "get_gameObject", 0); mGetComp = findMethod(kComp, "GetComponent", 1, "System.Type"); }
    { Klass* kG = findClass("UnityEngine", "GameObject"); if (kG) mSetActive = findMethod(kG, "SetActive", 1); Klass* kR = findClass("UnityEngine", "Renderer"); if (kR) mRendSetEn = findMethod(kR, "set_enabled", 1); }
    if (kLR) { mSetSW = findMethod(kLR, "set_startWidth", 1); mSetEW = findMethod(kLR, "set_endWidth", 1); }
    { Klass* kCol = findClass("UnityEngine", "Collider"); if (kCol) mGetBounds = findMethod(kCol, "get_bounds", 0); }
    if (kBall) mBallBody = findMethod(kBall, "get_Body", 0);
    if (kSpin) { mSpinAng = findMethod(kSpin, "get_SpinAngelDeg", 0); mSpinFactor = findMethod(kSpin, "get_SpinFactor", 0); }
    g_offIsHit = fieldOff(kGM, "<isHit>k__BackingField", off::GM_isHit); g_offFd = fieldOff(kPhysicsBuilder, "forceDirection", off::PB_forceDir); g_offTv = fieldOff(kPhysicsBuilder, "targetBallFinalVelocity", off::PB_targetVel);
    g_offLv = fieldOff(kPhysicsBuilder, "levelMaxPower", off::PB_levelMax); g_offAfter = fieldOff(kCueBall, "forceDirectionAfterCollided", off::CB_afterCollided); g_offTgt = fieldOff(kCueBall, "collidedBallForceDirection", off::CB_targetDir); g_offColl = fieldOff(kCueBall, "collidedBall", off::CB_collidedBall);
    { int a = -1; for (const char* nm : {"isAiming", "IsAiming", "<isAiming>k__BackingField", "<IsAiming>k__BackingField", "aiming", "isAim"}) { a = fieldOff(kCueBall, nm, -1); if (a > 0) { logFile("[4] CueBall aim flag resolved by name \"%s\" at 0x%x (hard-coded was 0x%x)", nm, a, off::CB_isAiming); break; } } if (a > 0) g_offAim = a; else logFile("[4] CueBall aim flag: no field by name, hard-coded 0x%x + force fallback", off::CB_isAiming); }
    logFile("[4] offsets: isHit=0x%x forceDirection=0x%x targetVel=0x%x levelMax=0x%x afterCollided=0x%x targetDir=0x%x collidedBall=0x%x | Ball.get_Body=%d SpinAngelDeg=%d SpinFactor=%d", g_offIsHit, g_offFd, g_offTv, g_offLv, g_offAfter, g_offTgt, g_offColl, mBallBody != nullptr, mSpinAng != nullptr, mSpinFactor != nullptr);
    if (!kObj || !kComp || !kTr || !kLR) return false;
    mGetTransform = findMethod(kComp, "get_transform", 0); mGetPosition = findMethod(kTr, "get_position", 0);
    mInstantiate = findMethod(kObj, "Instantiate", 1, "UnityEngine.Object"); mFindObjs = findMethod(kObj, "FindObjectsByType", 2, "System.Type");
    mSetCount = findMethod(kLR, "set_positionCount", 1); mSetPos = findMethod(kLR, "SetPosition", 2); mSetWorld = findMethod(kLR, "set_useWorldSpace", 1); mSetC0 = findMethod(kLR, "set_startColor", 1); mSetC1 = findMethod(kLR, "set_endColor", 1);
    const Method* all[] = {mGetTransform, mGetPosition, mInstantiate, mSetCount, mSetPos, mSetWorld, mSetC0, mSetC1};
    for (auto m : all) if (!m || !fnptr(m)) return false;
    return true;
}
static void initGame() {
    g_stage = 29; logFile("[3] waiting 20s for the game's runtime to finish starting before the first il2cpp call");
    for (int i = 0; i < 40; ++i) usleep(500000);
    g_stage = 30; Domain* dom = nullptr; for (int i = 0; i < 240 && !dom; ++i) { dom = il2cpp_domain_get(); if (!dom) usleep(500000); }
    if (!dom) { logFile("[3] il2cpp domain never became available"); return; }
    g_stage = 31; if (il2cpp_thread_attach) il2cpp_thread_attach(dom);
    g_stage = 32; bool have = false;
    for (int i = 0; i < 240 && !have; ++i) { size_t n = 0; Assembly** as = il2cpp_domain_get_assemblies(dom, &n);
        for (size_t k = 0; as && k < n && !have; ++k) { const char* nm = il2cpp_image_get_name(il2cpp_assembly_get_image(as[k])); if (nm && strstr(nm, "Assembly-CSharp")) have = true; } if (!have) usleep(500000); }
    if (!have) { logFile("[3] Assembly-CSharp never appeared"); return; }
    g_stage = 33; Klass* kv = nullptr; for (int i = 0; i < 240 && !kv; ++i) { kv = findClass("", "CueBallView"); if (!kv) usleep(500000); }
    if (!kv) { logFile("[3] class CueBallView not found"); return; }
    g_stage = 40; const Method* m = findMethod(kv, "LateUpdate", 0); if (!m) { logFile("[4] CueBallView.LateUpdate not found - NOT hooking"); return; }
    if (!resolve()) { logFile("[4] could not resolve Unity methods - NOT hooking"); return; }
    g_stage = 45; { std::string rep; g_hostInit = true; g_hostOk = g_host.init(rep); std::string line; for (char ch : rep) { if (ch == '\n') { logFile("[H] %s", line.c_str()); line.clear(); } else line += ch; }
      logFile("[H] exact-path host: %s", g_hostOk ? "READY" : "NOT READY (the faint preview still works)"); }
    g_stage = 46; runProbe("launch");
    g_stage = 50; loadCfg(); g_probeDone = g_cfg.probe; g_recDone = g_cfg.rec; g_orig = (void (*)(void*, const Method*))fnptr(m); __atomic_store_n(&fnptr(m), (void*)&hooked, __ATOMIC_SEQ_CST);
    { bool sc = false; Klass* kst = ppu::probeClass("*", "ServerTracker", sc); const Method* mf = kst ? findMethod(kst, "FixedUpdate", 0) : nullptr;
      if (mf && fnptr(mf) && !g_fixOn) { g_origFix = (void (*)(void*, const Method*))fnptr(mf); __atomic_store_n(&fnptr(mf), (void*)&hookedFix, __ATOMIC_SEQ_CST); g_fixOn = true; logFile("[R] ServerTracker.FixedUpdate hooked: the shot recorder samples every physics tick"); }
      else logFile("[R] ServerTracker.FixedUpdate %s: the shot recorder samples rendered frames instead", mf ? "already hooked or no code pointer" : "not found"); }
    logFile("hooked CueBallView.LateUpdate"); LOG("hooked");
    g_stage = 55; installGates();
}
static void bringUp() {
    bool apiOk = false;
    for (int i = 0; i < 240 && !apiOk; ++i) {
        g_stage = 1; void* h = dlopen("libil2cpp.so", RTLD_NOLOAD); if (h && il2::load(h)) { apiOk = true; break; }
        il2::Mod mod; g_stage = 2;
        if (il2::findModule("libil2cpp.so", mod)) { g_stage = 3; if (il2::loadMod(mod)) { apiOk = true; g_il2Base = mod.base; logFile("[2] il2cpp module %s base=%p nsym=%d", mod.path, (void*)mod.base, (int)mod.nsym); break; } }
        usleep(500000);
    }
    if (!apiOk) { logFile("[2] libil2cpp.so never appeared / exports unusable (waited 120s)"); return; }
    logFile("[2] il2cpp api OK (array_new=%d gchandle=%d resolve_icall=%d class_get_fields=%d)", il2cpp_array_new != nullptr, il2cpp_gchandle_new != nullptr, il2cpp_resolve_icall != nullptr, il2cpp_class_get_field_from_name != nullptr);
    initGame();
}
static void* initThread(void*) {
    logFile("[1] payload v84 LIVE loaded pid=%d uid=%d dir=%s", (int)getpid(), (int)getuid(), g_dir.c_str());
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = onSegv; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask); sigaction(SIGSEGV, &sa, &g_old);
    g_tidI = tidNow(); g_inI = 1;
    if (sigsetjmp(g_jbI, 1) == 0) bringUp(); else logFile("[X] FAULT during init at stage %d addr=%p pc=%p (libil2cpp+0x%lx) - hook aborted, game kept alive", (int)g_stage, (void*)g_fAddr, (void*)g_fPc, (unsigned long)(g_fPc - g_il2Base));
    g_inI = 0;
    for (;;) sleep(3600);   // park this thread forever (a runtime that registered it must never signal a thread that no longer exists)
    return nullptr;
}
__attribute__((constructor)) static void entry() { pthread_t t; pthread_create(&t, nullptr, initThread, nullptr); pthread_detach(t); }
