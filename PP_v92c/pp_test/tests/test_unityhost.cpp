// PP v84: UnityHost against a FAKE Unity (function pointers behind fake MethodInfos, il2cpp reflection stubbed).  Checks name resolution, the direct-call plumbing,
// snapshot/restore (rigidbody AND transform), contact / pocket detection and a full live::predict.  NOT a physics accuracy test.
#include "../native/unityhost.hpp"
#include "../native/paths.hpp"
#include <map>
#include <cstdio>
using namespace il2;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)

struct Obj0 { void* klass = nullptr; void* mon = nullptr; void* cached = (void*)0x200000; };
struct FRb : Obj0 { float p[3] = {0, -0.03f, 0}, v[3] = {0, 0, 0}, w[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}; bool sleeping = false; struct FTr* tr = nullptr; };
struct FTr : Obj0 { float p[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}; };
struct FCol : Obj0 { FRb* rb = nullptr; const char* tag = "Ball"; bool trig = false; };
struct FStr { void* k = nullptr; void* m = nullptr; int len; uint16_t ch[16]; };
struct FMethod { void* fn; const char* name; int argc; const char* first; };
struct FKlass { const char* name; std::vector<FMethod*> ms; };
static std::vector<FRb*> RBS; static std::vector<FCol*> COLS; static std::vector<FStr*> STRS; static int simCalls = 0; static float pocketX = 99.f;

// ---- fake Unity API (signatures exactly as unityhost.hpp casts them) ----
struct F3 { float x, y, z; }; struct F4 { float x, y, z, w; };
static F3 rb_getPos(void* s, const Method*) { auto* r = (FRb*)s; return {r->p[0], r->p[1], r->p[2]}; }
static void rb_setPos(void* s, F3 v, const Method*) { auto* r = (FRb*)s; r->p[0] = v.x; r->p[1] = v.y; r->p[2] = v.z; }
static F4 rb_getRot(void* s, const Method*) { auto* r = (FRb*)s; return {r->q[0], r->q[1], r->q[2], r->q[3]}; }
static void rb_setRot(void* s, F4 v, const Method*) { auto* r = (FRb*)s; r->q[0] = v.x; r->q[1] = v.y; r->q[2] = v.z; r->q[3] = v.w; }
static F3 rb_getVel(void* s, const Method*) { auto* r = (FRb*)s; return {r->v[0], r->v[1], r->v[2]}; }
static void rb_setVel(void* s, F3 v, const Method*) { auto* r = (FRb*)s; r->v[0] = v.x; r->v[1] = v.y; r->v[2] = v.z; r->sleeping = false; }
static F3 rb_getAng(void* s, const Method*) { auto* r = (FRb*)s; return {r->w[0], r->w[1], r->w[2]}; }
static void rb_setAng(void* s, F3 v, const Method*) { auto* r = (FRb*)s; r->w[0] = v.x; r->w[1] = v.y; r->w[2] = v.z; }
static void rb_sleep(void* s, const Method*) { ((FRb*)s)->sleeping = true; }
static bool rb_isSleeping(void* s, const Method*) { return ((FRb*)s)->sleeping; }
static void* cmp_tr(void* s, const Method*) { return ((FRb*)s)->tr; }
static void tr_setPos(void* s, F3 v, const Method*) { auto* t = (FTr*)s; t->p[0] = v.x; t->p[1] = v.y; t->p[2] = v.z; }
static void tr_setRot(void* s, F4 v, const Method*) { auto* t = (FTr*)s; t->q[0] = v.x; t->q[1] = v.y; t->q[2] = v.z; t->q[3] = v.w; }
static void* col_rb(void* s, const Method*) { return ((FCol*)s)->rb; }
static bool col_trig(void* s, const Method*) { return ((FCol*)s)->trig; }
static void* cmp_tag(void* s, const Method*) { auto* c = (FCol*)s; auto* st = new FStr(); st->len = (int)strlen(c->tag); for (int i = 0; i < st->len; ++i) st->ch[i] = (uint16_t)c->tag[i]; STRS.push_back(st); return st; }
static void ph_simulate(float dt, const Method*) { ++simCalls; for (auto* r : RBS) { if (r->sleeping) continue; for (int k = 0; k < 3; k += 2) { r->p[k] += r->v[k] * dt; r->v[k] *= (1.f - 0.62f * dt); } r->tr->p[0] = r->p[0]; r->tr->p[2] = r->p[2]; } }
static int ph_overlap(F3 pos, float rad, void* arr, int, int qti, const Method*) {
    void** el = (void**)((char*)arr + 0x20); int n = 0;
    for (auto* c : COLS) { if (c->trig && qti != 2) continue; FRb* r = c->rb; float dx = r ? r->p[0] - pos.x : pocketX - pos.x, dz = r ? r->p[2] - pos.z : -pos.z;
        const float reach = rad + (c->trig ? 0.01f : 0.0275f); if (r && std::fabs(dx) < 1e-6f && std::fabs(dz) < 1e-6f) continue; if (dx * dx + dz * dz <= reach * reach && n < 24) el[n++] = c; }
    return n;
}
static bool ph_sphereCast(F3, float, F3, void* hit, float, int, const Method*) { memset(hit, 0, 44); return false; }
static int g_fakeMode = 2;
static int ph_mode(const Method*) { return g_fakeMode; }
static void ph_setMode(int v, const Method*) { g_fakeMode = v; }
static float ph_contactOff(const Method*) { return 0.001f; }

// ---- fake il2cpp reflection ----
static std::map<std::string, FKlass*> KL; static FMethod* mk(FKlass* k, const char* nm, void* fn, int argc, const char* first = nullptr) { auto* m = new FMethod{fn, nm, argc, first}; k->ms.push_back(m); return m; }
static FKlass* K(const char* n) { auto* k = new FKlass{n, {}}; KL[n] = k; return k; }
static Domain* f_dom() { return (Domain*)0x1; }
static Assembly** f_asm(const Domain*, size_t* n) { static Assembly* a[1] = {(Assembly*)0x2}; *n = 1; return a; }
static Image* f_img(const Assembly*) { return (Image*)0x3; }
static Klass* f_cfn(const Image*, const char*, const char* nm) { auto it = KL.find(nm); return it == KL.end() ? nullptr : (Klass*)it->second; }
static const Method* f_cgm(Klass* k, void** it) { auto* kk = (FKlass*)k; size_t i = (size_t)*it; if (i >= kk->ms.size()) return nullptr; *it = (void*)(i + 1); return (const Method*)kk->ms[i]; }
static const char* f_mn(const Method* m) { return ((const FMethod*)m)->name; }
static uint32_t f_pc(const Method* m) { return (uint32_t)((const FMethod*)m)->argc; }
static const Type* f_mp(const Method* m, uint32_t) { return (const Type*)((const FMethod*)m)->first; }
static bool f_gen(const Method*) { return false; }
static char* f_tn(const Type* t) { return (char*)t; }
static void* f_arr(Klass*, size_t n) { void* p = calloc(n + 4, 8); return p; }
static uint32_t f_gch(Obj*, bool) { return 1; }

int main() {
    il2cpp_domain_get = f_dom; il2cpp_domain_get_assemblies = f_asm; il2cpp_assembly_get_image = f_img; il2cpp_class_from_name = f_cfn; il2cpp_class_get_methods = f_cgm; il2cpp_method_get_name = f_mn;
    il2cpp_method_get_param_count = f_pc; il2cpp_method_get_param = f_mp; il2cpp_method_is_generic = f_gen; il2cpp_type_get_name = f_tn; il2cpp_array_new = f_arr; il2cpp_gchandle_new = f_gch; il2cpp_resolve_icall = nullptr;
    FKlass *rb = K("Rigidbody"), *ph = K("Physics"), *col = K("Collider"), *comp = K("Component"), *tr = K("Transform"); K("GameObject");
    mk(rb, "get_position", (void*)rb_getPos, 0); mk(rb, "set_position", (void*)rb_setPos, 1); mk(rb, "get_rotation", (void*)rb_getRot, 0); mk(rb, "set_rotation", (void*)rb_setRot, 1);
    mk(rb, "get_linearVelocity", (void*)rb_getVel, 0); mk(rb, "set_linearVelocity", (void*)rb_setVel, 1); mk(rb, "get_angularVelocity", (void*)rb_getAng, 0); mk(rb, "set_angularVelocity", (void*)rb_setAng, 1);
    mk(rb, "Sleep", (void*)rb_sleep, 0); mk(rb, "IsSleeping", (void*)rb_isSleeping, 0);
    mk(comp, "get_transform", (void*)cmp_tr, 0); mk(comp, "get_tag", (void*)cmp_tag, 0); mk(tr, "set_position", (void*)tr_setPos, 1); mk(tr, "set_rotation", (void*)tr_setRot, 1);
    mk(col, "get_attachedRigidbody", (void*)col_rb, 0); mk(col, "get_isTrigger", (void*)col_trig, 0);
    mk(ph, "Simulate", (void*)ph_simulate, 1, "System.Single"); mk(ph, "OverlapSphereNonAlloc", (void*)ph_overlap, 5, "UnityEngine.Vector3"); mk(ph, "SphereCast", (void*)ph_sphereCast, 6, "UnityEngine.Vector3");
    mk(ph, "SphereCast", (void*)ph_sphereCast, 6, "UnityEngine.Ray");   // the Ray overload must NOT be picked
    mk(ph, "get_simulationMode", (void*)ph_mode, 0); mk(ph, "set_simulationMode", (void*)ph_setMode, 1); mk(ph, "get_defaultContactOffset", (void*)ph_contactOff, 0);

    ppu::UnityHost H; std::string rep; bool ok = H.init(rep); CHECK(ok && H.ready, "init resolves every name"); if (!ok) printf("%s", rep.c_str());
    CHECK(H.phSphereCast && std::string(((FMethod*)H.phSphereCast)->first) == "UnityEngine.Vector3", "SphereCast(Vector3..) overload chosen, not Ray");
    CHECK(H.simMode() == 2, "simulationMode readable");
    CHECK(H.canSetSimMode(), "set_simulationMode resolved");
    g_fakeMode = 0; CHECK(H.simMode() == 0, "client mode FixedUpdate readable");
    H.setSimMode(2); CHECK(H.simMode() == 2, "setSimMode(Script) takes effect"); H.setSimMode(0); CHECK(H.simMode() == 0, "setSimMode restores the original mode"); g_fakeMode = 2;
    { ppu::UnityHost H2; KL.erase("Physics"); std::string r2; CHECK(!H2.init(r2) && r2.find("MISSING") != std::string::npos, "missing class -> not ready, report says MISSING"); KL["Physics"] = ph; }

    // scene: cue (0) at -0.5, ball (1) at 0.0, ball (2) far away; a pocket trigger far right
    const char* tags[3] = {"Ball", "Ball", "Ball"};
    for (int i = 0; i < 3; ++i) { auto* r = new FRb(); auto* t = new FTr(); r->tr = t; RBS.push_back(r); auto* c = new FCol(); c->rb = r; c->tag = tags[i]; COLS.push_back(c); }
    RBS[0]->p[0] = -0.5f; RBS[1]->p[0] = 0.0f; RBS[2]->p[0] = 0.4f; RBS[2]->p[2] = 0.3f; RBS[1]->sleeping = true; RBS[2]->sleeping = true;
    for (auto* r : RBS) { r->tr->p[0] = r->p[0]; r->tr->p[2] = r->p[2]; }
    auto* trig = new FCol(); trig->rb = nullptr; trig->tag = "PocketInside"; trig->trig = true; COLS.push_back(trig); pocketX = 0.9f;
    std::vector<void*> bodies(RBS.begin(), RBS.end()); std::string why; H.ballRadius = 0.0275; H.readConstants();
    { bool sb = H.setBalls(bodies, 0, why); if (!sb) printf("setBalls why=%s\n", why.c_str()); CHECK(sb, "setBalls"); }
    H.setPockets({{0.9, 0.0}}, 0.3);
    std::vector<float> before; for (auto* r : RBS) { before.push_back(r->p[0]); before.push_back(r->p[2]); }
    pp2::live::ShotIn S; S.cueId = 0; S.dx = 1; S.dz = 0; S.F = 3.0; S.havePred = false;
    pp2::live::Options O; O.tmax = 2.0; O.recEvery = 2; O.moveEvery = 6; O.maxMs = 2000;
    auto R = pp2::live::predict(H, S, O);
    CHECK(R.ok && R.restored, "predict ok + restored"); CHECK(simCalls > 20, "Physics.Simulate was called");
    bool same = true; size_t k = 0; for (auto* r : RBS) { same = same && r->p[0] == before[k] && r->p[2] == before[k + 1]; k += 2; } CHECK(same, "every rigidbody is back where it was");
    bool trSame = true; for (auto* r : RBS) trSame = trSame && std::fabs(r->tr->p[0] - r->p[0]) < 1e-6 && std::fabs(r->tr->p[2] - r->p[2]) < 1e-6; CHECK(trSame, "every Transform equals its rigidbody again (restore writes both)");
    CHECK(RBS[1]->sleeping && RBS[2]->sleeping, "balls that were asleep are asleep again"); CHECK(!RBS[0]->sleeping, "the awake cue stays awake");
    bool contact = false, pocket = false; for (auto& e : R.events) { if (e.what.compare(0, 13, "first contact") == 0) contact = true; if (e.what == "pocket") pocket = true; }
    CHECK(contact, "first contact found through Physics.OverlapSphereNonAlloc + tags"); CHECK(R.frames.size() > 5, "frames recorded");
    auto S2 = ppu::buildScene(R); CHECK(!S2.paths.empty() && S2.paths[0].pts.size() >= 2, "scene has a cue path");
    printf("   (contact=%d pocket=%d ticks=%d steps=%d paths=%d marks=%d)\n", (int)contact, (int)pocket, R.ticks, R.steps, (int)S2.paths.size(), (int)S2.marks.size());
    // tag helper + Edge classification
    FCol* edge = new FCol(); edge->tag = "Edge"; CHECK(H.tagIs(edge, "Edge") && !H.tagIs(edge, "Ball"), "tagIs reads the managed string");
    // sim mode != Script is refused by the payload (simMode is what it checks)
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
