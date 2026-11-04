#include "pp2_il2cpp_host.hpp"
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
namespace pp2 { namespace il2 {

bool loadApi(void* h, Api& a, std::string& err) {
#define L(n) a.n = reinterpret_cast<decltype(a.n)>(dlsym(h, "il2cpp_" #n)); if (!a.n) err += " il2cpp_" #n;
    L(domain_get) L(domain_assembly_open) L(assembly_get_image) L(class_from_name) L(class_get_method_from_name) L(class_get_field_from_name) L(field_get_value)
    L(field_static_get_value) L(runtime_invoke) L(object_unbox) L(object_get_class) L(class_get_name) L(array_new) L(string_new) L(thread_attach) L(array_object_header_size)
#undef L
    return a.complete();
}

namespace {
const char* NS_MGR = "PoolLiveTourMax.Managers"; const char* NS_BALL = "PoolLiveTourMax.Gameplay.Balls"; const char* NS_PHYS = "PoolLiveTourMax.Physic.Handlers"; const char* NS_SHOT = "PoolLiveTourMax.Gameplay.Shot";
std::string utf16ToAscii(void* s) {   // Il2CppString: header 0x10, int32 length @0x10, UTF-16 chars @0x14
    if (!s) return ""; const int len = *(const int*)((const char*)s + 0x10); const uint16_t* c = (const uint16_t*)((const char*)s + 0x14); std::string o;
    for (int i = 0; i < len && i < 64; ++i) o += (char)c[i]; return o;
}
}

bool GameHost::init(std::string& rep) {
    bool ok = A.complete(); if (!ok) { rep += "MISSING il2cpp api table\n"; return false; }
    void* dom = A.domain_get(); A.thread_attach ? (void)A.thread_attach(dom) : (void)0;
    auto image = [&](const char* asmName) -> void* { void* as = A.domain_assembly_open(dom, asmName); return as ? A.assembly_get_image(as) : nullptr; };
    void *iPhys = image("UnityEngine.PhysicsModule"), *iCore = image("UnityEngine.CoreModule"), *iGame = image("Assembly-CSharp");
    auto K = [&](void* img, const char* ns, const char* nm, void*& out) { out = img ? A.class_from_name(img, ns, nm) : nullptr; rep += std::string(out ? "OK      class " : "MISSING class ") + ns + "." + nm + "\n"; if (!out) ok = false; };
    auto Me = [&](void* k, const char* nm, int argc, const void*& out) { out = k ? A.class_get_method_from_name(k, nm, argc) : nullptr; rep += std::string(out ? "OK      method " : "MISSING method ") + nm + "/" + std::to_string(argc) + "\n"; if (!out) ok = false; };
    K(iPhys, "UnityEngine", "Rigidbody", kRigid); K(iPhys, "UnityEngine", "Physics", kPhysics); K(iPhys, "UnityEngine", "Collider", kCollider); K(iCore, "UnityEngine", "Component", kComponent); K(iCore, "UnityEngine", "Time", kTime);
    K(iGame, NS_MGR, "GameManager", kGM); K(iGame, NS_BALL, "Ball", kBall); K(iGame, NS_BALL, "CueBall", kCue); K(iGame, NS_PHYS, "GamePhysics", kGP); K(iGame, NS_PHYS, "PhysicsBuilder", kPB); K(iGame, NS_SHOT, "SpinController", kSpin);
    Me(kRigid, "get_position", 0, rbGetPos); Me(kRigid, "set_position", 1, rbSetPos); Me(kRigid, "get_rotation", 0, rbGetRot); Me(kRigid, "set_rotation", 1, rbSetRot);
    Me(kRigid, "get_linearVelocity", 0, rbGetVel); Me(kRigid, "set_linearVelocity", 1, rbSetVel); Me(kRigid, "get_angularVelocity", 0, rbGetAng); Me(kRigid, "set_angularVelocity", 1, rbSetAng);
    Me(kRigid, "get_isKinematic", 0, rbGetKin); Me(kRigid, "get_linearDamping", 0, rbGetDamp); Me(kTime, "get_fixedTime", 0, timeGetFixed);
    Me(kRigid, "Sleep", 0, rbSleep); Me(kRigid, "IsSleeping", 0, rbIsSleeping); Me(kRigid, "WakeUp", 0, rbWake);
    Me(kPhysics, "Simulate", 1, phSimulate); Me(kPhysics, "OverlapSphereNonAlloc", 5, phOverlap); Me(kPhysics, "SphereCast", 6, phSphereCast); Me(kPhysics, "get_simulationMode", 0, phGetMode);
    Me(kCollider, "get_attachedRigidbody", 0, colAttached); Me(kCollider, "get_isTrigger", 0, colIsTrigger); Me(kComponent, "get_tag", 0, cmpCompareTag);
    Me(kGM, "get_Instance", 0, gmGetInstance); Me(kGM, "get_Balls", 0, gmGetBalls); Me(kBall, "get_Body", 0, ballGetBody); Me(kGP, "get_Instance", 0, gpGetInstance);
    auto F = [&](void* k, const char* nm) { void* f = k ? A.class_get_field_from_name(k, nm) : nullptr; rep += std::string(f ? "OK      field " : "MISSING field ") + nm + "\n"; if (!f) ok = false; };
    F(kGM, "<isHit>k__BackingField"); F(kGP, "physicsBuilder"); F(kPB, "forceDirection"); F(kPB, "targetBallFinalVelocity"); F(kCue, "forceDirectionAfterCollided"); F(kCue, "collidedBallForceDirection"); F(kCue, "collidedBall");
    ready_ = ok; return ok;
}

bool GameHost::refresh(std::string& why, bool forLog) {
    bodies_.clear(); cue_ = -1; if (!ready_) { why = "not initialised"; return false; }
    void* modeObj = inv(phGetMode, nullptr, nullptr); if (!forLog && (!modeObj || *(int*)A.object_unbox(modeObj) != 2)) { why = "Physics.simulationMode != Script (the game's loop would double-step)"; return false; }
    gm_ = inv(gmGetInstance, nullptr, nullptr); if (!gm_) { why = "GameManager.Instance null"; return false; }
    bool hit = true; A.field_get_value(gm_, A.class_get_field_from_name(kGM, "<isHit>k__BackingField"), &hit); if (hit && !forLog) { why = "GameManager.isHit == true (shot in progress)"; return false; }
    void* list = inv(gmGetBalls, gm_, nullptr); if (!list) { why = "Balls list null"; return false; }
    void* lk = A.object_get_class(list); const void* mCount = A.class_get_method_from_name(lk, "get_Count", 0); const void* mItem = A.class_get_method_from_name(lk, "get_Item", 1);
    if (!mCount || !mItem) { why = "List<Ball> accessors missing"; return false; }
    void* cn = inv(mCount, list, nullptr); const int n = cn ? *(int*)A.object_unbox(cn) : 0;
    for (int i = 0; i < n; ++i) {
        int idx = i; void* args[1] = {&idx}; void* b = inv(mItem, list, args); if (!b) { why = "null ball"; return false; }
        void* body = inv(ballGetBody, b, nullptr); if (!body) { why = "ball without Rigidbody"; return false; }
        bodies_.push_back(body); if (std::strcmp(A.class_get_name(A.object_get_class(b)), "CueBall") == 0) { cue_ = i; cueObj_ = b; cueClass_ = A.object_get_class(b); }
    }
    if (cue_ < 0) { why = "no CueBall in Balls"; return false; }
    return true;
}

bool GameHost::get(int i, live::BallState& s) {
    void* rb = bodies_[i]; float p[3], v[3], w[3], q[4]; if (!vec3(rbGetPos, rb, p) || !vec3(rbGetVel, rb, v) || !vec3(rbGetAng, rb, w) || !quat(rbGetRot, rb, q)) return false;
    s.id = i; s.pos = {p[0], p[1], p[2]}; s.vel = {v[0], v[1], v[2]}; s.ang = {w[0], w[1], w[2]}; for (int k = 0; k < 4; ++k) s.q[k] = q[k];
    void* sl = inv(rbIsSleeping, rb, nullptr); s.sleeping = sl && *(bool*)A.object_unbox(sl); s.active = true; return true;
}
bool GameHost::set(int i, const live::BallState& s) {
    void* rb = bodies_[i]; float p[3] = {(float)s.pos.x, (float)s.pos.y, (float)s.pos.z}, v[3] = {(float)s.vel.x, (float)s.vel.y, (float)s.vel.z}, w[3] = {(float)s.ang.x, (float)s.ang.y, (float)s.ang.z}, q[4] = {(float)s.q[0], (float)s.q[1], (float)s.q[2], (float)s.q[3]};
    void* a1[1]; a1[0] = p; inv(rbSetPos, rb, a1); a1[0] = q; inv(rbSetRot, rb, a1); a1[0] = v; inv(rbSetVel, rb, a1); a1[0] = w; inv(rbSetAng, rb, a1);
    if (s.sleeping) inv(rbSleep, rb, nullptr); return lastError.empty();
}
bool GameHost::step(double dt) { float f = (float)dt; void* a[1] = {&f}; inv(phSimulate, nullptr, a); return lastError.empty(); }

bool GameHost::castCue(int ci, live::Vec3 dir, double dist) {
    live::BallState c; if (!get(ci, c)) return false;
    float o[3] = {(float)c.pos.x, (float)c.pos.y, (float)c.pos.z}, d[3] = {(float)dir.x, 0.f, (float)dir.z}, r = (float)(ballRadius + 2 * contactOffset), md = (float)dist; int mask = castMask; alignas(8) unsigned char hit[96] = {0};
    void* a[6] = {o, &r, d, hit, &md, &mask}; void* res = inv(phSphereCast, nullptr, a); return res && *(bool*)A.object_unbox(res);
}
bool GameHost::tagIs(void* comp, const char* tag) {
    const void* m = A.class_get_method_from_name(A.object_get_class(comp), "get_tag", 0); if (!m) return false; void* s = inv(m, comp, nullptr); return utf16ToAscii(s) == tag;
}
live::Contact GameHost::cueContact(int ci) {
    live::Contact c; static void* arr = nullptr; if (!arr) arr = A.array_new(kCollider, 16); live::BallState cb; if (!get(ci, cb)) return c;
    float pos[3] = {(float)cb.pos.x, (float)cb.pos.y, (float)cb.pos.z}, r = (float)(ballRadius + 2 * contactOffset); int mask = -1, qti = 1; /* QueryTriggerInteraction.Ignore */
    void* a[5] = {pos, &r, arr, &mask, &qti}; void* cn = inv(phOverlap, nullptr, a); const int n = cn ? *(int*)A.object_unbox(cn) : 0; void** el = (void**)((char*)arr + A.array_object_header_size());
    for (int i = 0; i < n && i < 16; ++i) {
        void* col = el[i]; if (!col) continue; void* rb = inv(colAttached, col, nullptr); if (rb == bodies_[ci]) continue; if (tagIs(col, "Table")) continue;   // the game excludes only tag "Table" (P2R)
        c.touched = true; const int bi = rb ? bodyIndex(rb) : -1; if (bi >= 0) { c.ballId = bi; c.edge = false; return c; } c.edge = true;
    }
    return c;
}
bool GameHost::inPocket(int idx) {
    static void* arr = nullptr; if (!arr) arr = A.array_new(kCollider, 16); live::BallState b; if (!get(idx, b)) return false;
    float pos[3] = {(float)b.pos.x, (float)b.pos.y, (float)b.pos.z}, r = (float)ballRadius; int mask = -1, qti = 2; /* Collide: include triggers (P2T: pocket = trigger event) */
    void* a[5] = {pos, &r, arr, &mask, &qti}; void* cn = inv(phOverlap, nullptr, a); const int n = cn ? *(int*)A.object_unbox(cn) : 0; void** el = (void**)((char*)arr + A.array_object_header_size());
    for (int i = 0; i < n && i < 16; ++i) { void* col = el[i]; if (!col) continue; void* t = inv(colIsTrigger, col, nullptr); if (t && *(bool*)A.object_unbox(t) && tagIs(col, "PocketInside")) return true; }
    return false;
}
void GameHost::park(int idx) { live::BallState s; s.id = idx; s.pos = {0, -100, 0}; s.q[3] = 1; set(idx, s); }

live::ShotIn GameHost::readShot() {
    live::ShotIn s; s.cueId = cue_; void* gp = inv(gpGetInstance, nullptr, nullptr); void* pb = nullptr; if (gp) A.field_get_value(gp, A.class_get_field_from_name(kGP, "physicsBuilder"), &pb);
    float fd[3] = {0, 0, 0}, tv = 0; if (pb) { A.field_get_value(pb, A.class_get_field_from_name(kPB, "forceDirection"), fd); A.field_get_value(pb, A.class_get_field_from_name(kPB, "targetBallFinalVelocity"), &tv); }
    s.dx = fd[0]; s.dz = fd[2]; s.F = std::sqrt((double)fd[0] * fd[0] + (double)fd[2] * fd[2]); s.targetVel = tv;
    if (cueObj_) {
        float cd[3] = {0, 0, 0}, td[3] = {0, 0, 0}; void* col = nullptr; A.field_get_value(cueObj_, A.class_get_field_from_name(kCue, "forceDirectionAfterCollided"), cd);
        A.field_get_value(cueObj_, A.class_get_field_from_name(kCue, "collidedBallForceDirection"), td); A.field_get_value(cueObj_, A.class_get_field_from_name(kCue, "collidedBall"), &col);
        s.collisionDir = {cd[0], 0, cd[2]}; s.targetDir = {td[0], 0, td[2]};
        if (col) { void* rb = inv(colAttached, col, nullptr); s.targetId = rb ? bodyIndex(rb) : -1; s.predEdge = (s.targetId < 0) || tagIs(col, "Edge"); s.havePred = true; }
    }
    // spinValue: SpinController.SpinFactor (ShotData.spinValue, P2H); spinUsed (ShotData.SpinApplied) is NOT resolved yet -> false (docs/FINDINGS_P2Z.md, assumption A4).
    void* sk = A.class_from_name(A.assembly_get_image(A.domain_assembly_open(A.domain_get(), "Assembly-CSharp")), NS_SHOT, "SpinController"); const void* gi = sk ? A.class_get_method_from_name(sk, "get_Instance", 0) : nullptr;
    const void* gs = sk ? A.class_get_method_from_name(sk, "get_SpinFactor", 0) : nullptr; void* sc = gi ? inv(gi, nullptr, nullptr) : nullptr; void* sf = (sc && gs) ? inv(gs, sc, nullptr) : nullptr;
    s.spinValue = sf ? *(float*)A.object_unbox(sf) : -1.0; s.spinUsed = false; return s;
}

// ---- P2Z4: per-tick recorder ----
bool GameHost::sampleTick(TickRec& o, std::string& why) {
    o = TickRec(); if (!refresh(why, true)) return false;
    void* t = inv(timeGetFixed, nullptr, nullptr); if (t) o.fixedTime = *(float*)A.object_unbox(t);
    void* m = inv(phGetMode, nullptr, nullptr); if (m) o.mode = *(int*)A.object_unbox(m);
    bool hit = false; A.field_get_value(gm_, A.class_get_field_from_name(kGM, "<isHit>k__BackingField"), &hit); o.hit = hit ? 1 : 0;
    for (size_t i = 0; i < bodies_.size(); ++i) {
        TickBall tb; void* rb = bodies_[i]; float p[3], v[3], w[3];
        const bool a = vec3(rbGetPos, rb, p), b = vec3(rbGetVel, rb, v), c = vec3(rbGetAng, rb, w);
        if (a) tb.pos = {p[0], p[1], p[2]}; if (b) tb.vel = {v[0], v[1], v[2]}; if (c) tb.ang = {w[0], w[1], w[2]};
        void* k = inv(rbGetKin, rb, nullptr); if (k) tb.kin = *(bool*)A.object_unbox(k) ? 1 : 0;
        void* d = inv(rbGetDamp, rb, nullptr); if (d) tb.damp = *(float*)A.object_unbox(d);
        void* s = inv(rbIsSleeping, rb, nullptr); if (s) tb.sleeping = *(bool*)A.object_unbox(s) ? 1 : 0;
        tb.ok = a && b && c; o.b.push_back(tb);
    }
    if (!lastError.empty()) { why = lastError; lastError.clear(); return false; }
    return true;
}
std::string GameHost::formatTick(const TickRec& r, long n) {
    char buf[256]; std::string s; std::snprintf(buf, sizeof buf, "#%ld T=%.6f mode=%d hit=%d", n, r.fixedTime, r.mode, r.hit); s = buf;
    for (size_t i = 0; i < r.b.size(); ++i) {
        const TickBall& b = r.b[i]; if (!b.ok) { std::snprintf(buf, sizeof buf, " %zu:ERR", i); s += buf; continue; }
        std::snprintf(buf, sizeof buf, " %zu:%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%d|%.4f|%d", i, b.pos.x, b.pos.y, b.pos.z, b.vel.x, b.vel.y, b.vel.z, b.kin, b.damp, b.sleeping); s += buf;
    }
    return s;
}
bool GameHost::looksBroken(const TickRec& r) {
    if (r.b.empty()) return true;
    for (const TickBall& b : r.b) if (!b.ok || b.pos.x != 0 || b.pos.y != 0 || b.pos.z != 0 || b.vel.x != 0 || b.vel.y != 0 || b.vel.z != 0) return false;
    return true;
}
}}  // namespace pp2::il2
