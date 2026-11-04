// PP v84 -- live::Host for the running game, built on DIRECT method-pointer calls (the same technique the proven v83 payload uses for Transform / LineRenderer),
// instead of il2cpp_runtime_invoke (PP2's pp2_il2cpp_host.cpp, never run on a device).  Everything is resolved BY NAME; every Unity call has a fallback chain:
//   managed method (several names)  ->  Unity internal call (il2cpp_resolve_icall, "_Injected" variants)  ->  reported as MISSING (the predictor then stays off, the draft engine keeps drawing).
// Main thread only.  No Physics.Simulate is ever issued unless every piece below resolved AND Physics.simulationMode == Script (else the game would be double-stepped).
#pragma once
#include "il2cpp.hpp"
#include "live.hpp"
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#include <climits>

namespace ppu {
using namespace il2;

struct F3 { float x, y, z; };
struct F4 { float x, y, z, w; };
struct Hit { float p[3], n[3]; int face; float dist; float uv[2]; int col; };   // UnityEngine.RaycastHit (44 bytes)

template <class T> static inline T rdp(const void* base, int o) { return *(const T*)((const char*)base + o); }
static inline bool okPtr(const void* p) { return (uintptr_t)p > 0x10000 && (uintptr_t)p < 0x0000800000000000ULL; }
static inline bool okObj(const void* o) { return okPtr(o) && okPtr(rdp<void*>(o, 0x10)); }   // UnityEngine.Object: m_CachedPtr @0x10 still set

// One Unity call that may be a managed method or an icall.  kind: 0 none, 1 managed (extra MethodInfo* arg), 2 icall plain, 3 icall "_Injected" (out/ref pointer)
struct Acc {
    const Method* m = nullptr; void* ic = nullptr; int kind = 0; const char* how = "-";
    bool ok() const { return kind != 0; }
};
static Acc findAcc(Klass* k, std::initializer_list<const char*> names, int argc, std::initializer_list<const char*> ics) {
    Acc a;
    if (k) for (const char* n : names) { const Method* m = findMethod(k, n, argc); if (m && fnptr(m)) { a.m = m; a.kind = 1; a.how = "M"; return a; } }
    if (il2cpp_resolve_icall) for (const char* c : ics) { void* f = il2cpp_resolve_icall(c); if (f) { a.ic = f; a.kind = strstr(c, "_Injected") ? 3 : 2; a.how = "I"; return a; } }
    return a;
}
// getters
static bool getF3(const Acc& a, void* self, F3& o) {
    switch (a.kind) { case 1: o = ((F3 (*)(void*, const Method*))fnptr(a.m))(self, a.m); return true;
        case 2: o = ((F3 (*)(void*))a.ic)(self); return true;
        case 3: ((void (*)(void*, F3*))a.ic)(self, &o); return true; }
    return false;
}
static bool getF4(const Acc& a, void* self, F4& o) {
    switch (a.kind) { case 1: o = ((F4 (*)(void*, const Method*))fnptr(a.m))(self, a.m); return true;
        case 2: o = ((F4 (*)(void*))a.ic)(self); return true;
        case 3: ((void (*)(void*, F4*))a.ic)(self, &o); return true; }
    return false;
}
// setters
static bool setF3(const Acc& a, void* self, F3 v) {
    switch (a.kind) { case 1: ((void (*)(void*, F3, const Method*))fnptr(a.m))(self, v, a.m); return true;
        case 2: ((void (*)(void*, F3))a.ic)(self, v); return true;
        case 3: ((void (*)(void*, F3*))a.ic)(self, &v); return true; }
    return false;
}
static bool setF4(const Acc& a, void* self, F4 v) {
    switch (a.kind) { case 1: ((void (*)(void*, F4, const Method*))fnptr(a.m))(self, v, a.m); return true;
        case 2: ((void (*)(void*, F4))a.ic)(self, v); return true;
        case 3: ((void (*)(void*, F4*))a.ic)(self, &v); return true; }
    return false;
}
static bool callV(const Acc& a, void* self) {
    switch (a.kind) { case 1: ((void (*)(void*, const Method*))fnptr(a.m))(self, a.m); return true; case 2: case 3: ((void (*)(void*))a.ic)(self); return true; }
    return false;
}
static int callB(const Acc& a, void* self) {   // -1 = unavailable
    switch (a.kind) { case 1: return ((bool (*)(void*, const Method*))fnptr(a.m))(self, a.m) ? 1 : 0; case 2: case 3: return ((bool (*)(void*))a.ic)(self) ? 1 : 0; }
    return -1;
}
static bool setBool(const Acc& a, void* self, bool v) {
    switch (a.kind) { case 1: ((void (*)(void*, bool, const Method*))fnptr(a.m))(self, v, a.m); return true; case 2: case 3: ((void (*)(void*, bool))a.ic)(self, v); return true; }
    return false;
}
static void* callP(const Acc& a, void* self) {
    switch (a.kind) { case 1: return ((void* (*)(void*, const Method*))fnptr(a.m))(self, a.m); case 2: case 3: return ((void* (*)(void*))a.ic)(self); }
    return nullptr;
}

// Il2CppString -> ascii (header 0x10, int32 length @0x10, UTF-16 @0x14)
static inline std::string strOf(void* s, size_t cap = 48) {
    std::string o; if (!okPtr(s)) return o; const int len = rdp<int>(s, 0x10); if (len < 0 || len > 4096) return o;
    const uint16_t* c = (const uint16_t*)((const char*)s + 0x14); for (int i = 0; i < len && (size_t)i < cap; ++i) o += (char)c[i]; return o;
}

class UnityHost : public pp2::live::Host {
public:
    using Vec3 = pp2::live::Vec3; using BallState = pp2::live::BallState; using Contact = pp2::live::Contact;

    // ---- resolved game classes / members (public: the payload reads shot data through them) ----
    Klass *kRb = nullptr, *kPhys = nullptr, *kCol = nullptr, *kComp = nullptr;
    Acc cmpTr, trSetPos, trSetRot;   // Component.get_transform + Transform.set_position / set_rotation: restore writes the Transform too (Unity only copies Rigidbody -> Transform on the NEXT Simulate)
    Klass* kTrans = nullptr;
    Acc rbGetPos, rbSetPos, rbGetRot, rbSetRot, rbGetVel, rbSetVel, rbGetAng, rbSetAng, rbSleep, rbIsSleeping, rbWake, rbGetKin, rbSetKin, colRb, colTrig, cmpTag, cmpGo, goTag;
    Klass* kGo = nullptr;
    const Method *phSimulate = nullptr, *phOverlap = nullptr, *phSphereCast = nullptr, *phGetMode = nullptr, *phSetMode = nullptr, *phContactOff = nullptr;
    Acc phModeIc, phSetModeIc;        // Physics.simulationMode getter / setter as icall (fallback)
    double ballRadius = 0.0275, contactOffset = 0.001; int castMask = 0xC00;
    std::string lastError;
    bool ready = false;

    // Dry probe: resolves names only; touches nothing in the game.  One line per item.
    bool init(std::string& rep) {
        bool ok = true;
        kRb = findClass("UnityEngine", "Rigidbody"); kPhys = findClass("UnityEngine", "Physics"); kCol = findClass("UnityEngine", "Collider"); kComp = findClass("UnityEngine", "Component");
        auto K = [&](Klass* k, const char* n) { rep += std::string(k ? "OK      class " : "MISSING class ") + n + "\n"; if (!k) ok = false; };
        K(kRb, "Rigidbody"); K(kPhys, "Physics"); K(kCol, "Collider"); K(kComp, "Component");
        auto A = [&](Acc& a, const char* nm, Klass* k, std::initializer_list<const char*> names, int argc, std::initializer_list<const char*> ics, bool required = true) {
            a = findAcc(k, names, argc, ics); char b[96]; snprintf(b, sizeof b, "%-8s %s [%s]\n", a.ok() ? "OK" : (required ? "MISSING" : "absent"), nm, a.how); rep += b; if (!a.ok() && required) ok = false; };
        A(rbGetPos, "Rigidbody.get_position", kRb, {"get_position"}, 0, {"UnityEngine.Rigidbody::get_position_Injected"});
        A(rbSetPos, "Rigidbody.set_position", kRb, {"set_position"}, 1, {"UnityEngine.Rigidbody::set_position_Injected"});
        A(rbGetRot, "Rigidbody.get_rotation", kRb, {"get_rotation"}, 0, {"UnityEngine.Rigidbody::get_rotation_Injected"});
        A(rbSetRot, "Rigidbody.set_rotation", kRb, {"set_rotation"}, 1, {"UnityEngine.Rigidbody::set_rotation_Injected"});
        A(rbGetVel, "Rigidbody.get_linearVelocity", kRb, {"get_linearVelocity", "get_velocity"}, 0, {"UnityEngine.Rigidbody::get_linearVelocity_Injected", "UnityEngine.Rigidbody::get_velocity_Injected"});
        A(rbSetVel, "Rigidbody.set_linearVelocity", kRb, {"set_linearVelocity", "set_velocity"}, 1, {"UnityEngine.Rigidbody::set_linearVelocity_Injected", "UnityEngine.Rigidbody::set_velocity_Injected"});
        A(rbGetAng, "Rigidbody.get_angularVelocity", kRb, {"get_angularVelocity"}, 0, {"UnityEngine.Rigidbody::get_angularVelocity_Injected"});
        A(rbSetAng, "Rigidbody.set_angularVelocity", kRb, {"set_angularVelocity"}, 1, {"UnityEngine.Rigidbody::set_angularVelocity_Injected"});
        A(rbSleep, "Rigidbody.Sleep", kRb, {"Sleep"}, 0, {"UnityEngine.Rigidbody::Sleep"}, false);          // optional: only used to re-sleep balls that were asleep
        A(rbIsSleeping, "Rigidbody.IsSleeping", kRb, {"IsSleeping"}, 0, {"UnityEngine.Rigidbody::IsSleeping"}, false);
        A(rbWake, "Rigidbody.WakeUp", kRb, {"WakeUp"}, 0, {"UnityEngine.Rigidbody::WakeUp"}, false);
        A(rbGetKin, "Rigidbody.get_isKinematic", kRb, {"get_isKinematic"}, 0, {"UnityEngine.Rigidbody::get_isKinematic"}, false);   // v92: the game's balls are KINEMATIC on a client (server drives them): a kinematic body ignores velocity / Simulate
        A(rbSetKin, "Rigidbody.set_isKinematic", kRb, {"set_isKinematic"}, 1, {"UnityEngine.Rigidbody::set_isKinematic"}, false);
        A(colRb, "Collider.get_attachedRigidbody", kCol, {"get_attachedRigidbody"}, 0, {"UnityEngine.Collider::get_attachedRigidbody"});
        A(colTrig, "Collider.get_isTrigger", kCol, {"get_isTrigger"}, 0, {"UnityEngine.Collider::get_isTrigger"});
        kGo = findClass("UnityEngine", "GameObject"); kTrans = findClass("UnityEngine", "Transform");
        A(cmpTr, "Component.get_transform", kComp, {"get_transform"}, 0, {"UnityEngine.Component::get_transform"}, false);
        A(trSetPos, "Transform.set_position", kTrans, {"set_position"}, 1, {"UnityEngine.Transform::set_position_Injected"}, false);
        A(trSetRot, "Transform.set_rotation", kTrans, {"set_rotation"}, 1, {"UnityEngine.Transform::set_rotation_Injected"}, false);
        A(cmpTag, "Component.get_tag", kComp, {"get_tag"}, 0, {"UnityEngine.Component::get_tag"}, false);
        if (!cmpTag.ok()) {   // fallback: Component.gameObject -> GameObject.tag
            A(cmpGo, "Component.get_gameObject", kComp, {"get_gameObject"}, 0, {"UnityEngine.Component::get_gameObject"});
            A(goTag, "GameObject.get_tag", kGo, {"get_tag"}, 0, {"UnityEngine.GameObject::get_tag"});
        }
        auto M = [&](const Method*& m, const char* nm, Klass* k, const char* name, int argc, const char* first, bool required = true, bool loose = false) {
            m = k ? findMethod(k, name, argc, first) : nullptr; if (!m && loose && k) m = findMethod(k, name, argc); if (m && !fnptr(m)) m = nullptr; char b[96]; snprintf(b, sizeof b, "%-8s %s\n", m ? "OK" : (required ? "MISSING" : "absent"), nm); rep += b; if (!m && required) ok = false; };
        M(phSimulate, "Physics.Simulate(float)", kPhys, "Simulate", 1, "System.Single", true, true);   // loose: the only 1-arg overload
        M(phOverlap, "Physics.OverlapSphereNonAlloc(5)", kPhys, "OverlapSphereNonAlloc", 5, "UnityEngine.Vector3");
        M(phSphereCast, "Physics.SphereCast(Vector3,..6)", kPhys, "SphereCast", 6, "UnityEngine.Vector3");
        M(phGetMode, "Physics.get_simulationMode", kPhys, "get_simulationMode", 0, nullptr);
        M(phSetMode, "Physics.set_simulationMode", kPhys, "set_simulationMode", 1, nullptr, false);   // optional: lets the exact path switch to Script for the duration of ONE synchronous prediction (P2Z-fix)
        M(phContactOff, "Physics.get_defaultContactOffset", kPhys, "get_defaultContactOffset", 0, nullptr, false);
        if (!phGetMode) { phModeIc = findAcc(nullptr, {}, 0, {"UnityEngine.Physics::get_simulationMode"}); if (phModeIc.ok()) { rep += "OK       Physics.get_simulationMode [I] (icall fallback)\n"; ok = ok && true; } }
        if (!phSetMode) { phSetModeIc = findAcc(nullptr, {}, 1, {"UnityEngine.Physics::set_simulationMode"}); if (phSetModeIc.ok()) rep += "OK       Physics.set_simulationMode [I] (icall fallback)\n"; }
        if (!il2cpp_array_new) { rep += "MISSING il2cpp_array_new export (Collider[] buffer)\n"; ok = false; }
        if (!il2cpp_gchandle_new) rep += "absent   il2cpp_gchandle_new (the Collider[] buffer is not pinned: GC could free it)\n";
        if (phGetMode || phModeIc.ok()) ok = ok;   // simulationMode readable
        else { rep += "MISSING Physics.simulationMode getter (live predictor refuses to run: cannot prove Script mode)\n"; ok = false; }
        ready = ok; return ok;   // names only: nothing in here calls into Unity (the init thread is not the main thread)
    }
    // main thread, first use: Physics.defaultContactOffset (P0 = 0.001; read from the game when the getter exists)
    void readConstants() { if (constRead_) return; constRead_ = true; if (phContactOff) { const double v = (double)((float (*)(const Method*))fnptr(phContactOff))(phContactOff); if (v > 1e-5 && v < 0.1) contactOffset = v; } }

    // Physics.simulationMode: 0 FixedUpdate, 1 Update, 2 Script, -1 unreadable
    int simMode() {
        if (phGetMode) return ((int (*)(const Method*))fnptr(phGetMode))(phGetMode);
        if (phModeIc.ok()) return ((int (*)())phModeIc.ic)();
        return -1;
    }

    // Physics.simulationMode setter.  The game itself only runs Script mode on the SERVER instance (ServerTracker.OnNetworkSpawn, P2P); a normal client is in FixedUpdate mode,
    // where Physics.Simulate is ignored by Unity.  The exact path therefore flips the mode to Script for the length of one synchronous prediction (main thread: no FixedUpdate can
    // interleave) and puts the original value back immediately (also from the fault handler).
    bool canSetSimMode() const { return phSetMode || phSetModeIc.ok(); }
    bool setSimMode(int v) {
        if (phSetMode) { ((void (*)(int, const Method*))fnptr(phSetMode))(v, phSetMode); return true; }
        if (phSetModeIc.ok()) { ((void (*)(int))phSetModeIc.ic)(v); return true; }
        return false;
    }

    // Reads the ball list.  balls = the Rigidbody of every ball in game order (index = list index = ball "id" used by the predictor), cueIdx = the CueBall.
    // bodyOf(ball) is supplied by the payload (Ball.get_Body or GetComponent(Rigidbody)); a ball without a body invalidates the whole snapshot.
    bool setBalls(const std::vector<void*>& bodies, int cueIdx, std::string& why) {
        bodies_.clear(); cue_ = -1;
        if (!ready) { why = "host not ready"; return false; }
        for (void* b : bodies) { if (!okObj(b)) { why = "a ball has no live Rigidbody"; return false; } bodies_.push_back(b); }
        if (cueIdx < 0 || cueIdx >= (int)bodies_.size()) { why = "cue ball not in list"; return false; }
        cue_ = cueIdx; trs_.clear(); if (cmpTr.ok()) for (void* b : bodies_) trs_.push_back(callP(cmpTr, b));
        if (!arr_) {
            Klass* ck = kCol; if (!ck || !il2cpp_array_new) { why = "no Collider[] buffer"; return false; }
            arr_ = il2cpp_array_new(ck, 24); if (!arr_) { why = "il2cpp_array_new failed"; return false; }
            if (il2cpp_gchandle_new) il2cpp_gchandle_new((Obj*)arr_, false);   // strong handle for the lifetime of the process (never freed)
        }
        return true;
    }
    void setPockets(const std::vector<std::pair<double, double>>& p, double mouth) { pockets_ = p; pocketNear_ = mouth; }
    int cueIndex() const { return cue_; }
    int bodyCount() const { return (int)bodies_.size(); }

    // ---- pp2::live::Host ----
    int count() override { return (int)bodies_.size(); }
    bool get(int i, BallState& s) override {
        void* rb = bodies_[i]; F3 p, v, w; F4 q;
        if (!getF3(rbGetPos, rb, p) || !getF3(rbGetVel, rb, v) || !getF3(rbGetAng, rb, w) || !getF4(rbGetRot, rb, q)) return false;
        s.id = i; s.pos = {p.x, p.y, p.z}; s.vel = {v.x, v.y, v.z}; s.ang = {w.x, w.y, w.z}; s.q[0] = q.x; s.q[1] = q.y; s.q[2] = q.z; s.q[3] = q.w;
        s.sleeping = rbIsSleeping.ok() && callB(rbIsSleeping, rb) == 1; s.active = true; return true;
    }
    bool set(int i, const BallState& s) override {
        void* rb = bodies_[i];
        bool ok = setF3(rbSetPos, rb, {(float)s.pos.x, (float)s.pos.y, (float)s.pos.z}) && setF4(rbSetRot, rb, {(float)s.q[0], (float)s.q[1], (float)s.q[2], (float)s.q[3]})
               && setF3(rbSetVel, rb, {(float)s.vel.x, (float)s.vel.y, (float)s.vel.z}) && setF3(rbSetAng, rb, {(float)s.ang.x, (float)s.ang.y, (float)s.ang.z});
        if (cmpTr.ok() && trSetPos.ok() && trSetRot.ok()) {   // keep the Transform equal to the restored pose (otherwise the end-of-simulation pose shows for one frame)
            void* tr = i < (int)trs_.size() ? trs_[i] : nullptr; if (okObj(tr)) { setF3(trSetPos, tr, {(float)s.pos.x, (float)s.pos.y, (float)s.pos.z}); setF4(trSetRot, tr, {(float)s.q[0], (float)s.q[1], (float)s.q[2], (float)s.q[3]}); }
        }
        if (s.sleeping && rbSleep.ok()) callV(rbSleep, rb);
        return ok;
    }
    // v92: kinematic balls (client of the game's server) are made dynamic for the prediction and put back as they were afterwards (Guard::take -> prepare, Guard::restore -> finish).
    int kinCount = 0; std::vector<char> kin_;
    void prepare() override {
        kinCount = 0; kin_.assign(bodies_.size(), 0); if (!rbGetKin.ok() || !rbSetKin.ok()) return;
        for (size_t i = 0; i < bodies_.size(); ++i) if (callB(rbGetKin, bodies_[i]) == 1) { kin_[i] = 1; setBool(rbSetKin, bodies_[i], false); ++kinCount; }
    }
    void finish() override { for (size_t i = 0; i < kin_.size() && i < bodies_.size(); ++i) if (kin_[i]) setBool(rbSetKin, bodies_[i], true); std::fill(kin_.begin(), kin_.end(), 0); }
    bool step(double dt) override { if (!phSimulate) return false; ((void (*)(float, const Method*))fnptr(phSimulate))((float)dt, phSimulate); return true; }
    double fixedDelta() override { return 1411199.0 / 141120000.0; }   // P2I: RationalTime from data.unity3d
    bool castCue(int ci, Vec3 dir, double dist) override {
        BallState c; if (!get(ci, c)) return false; Hit h{}; const float r = (float)(ballRadius + 2.0 * contactOffset);
        return ((bool (*)(F3, float, F3, Hit*, float, int, const Method*))fnptr(phSphereCast))(F3{(float)c.pos.x, (float)c.pos.y, (float)c.pos.z}, r, F3{(float)dir.x, 0.f, (float)dir.z}, &h, (float)dist, castMask, phSphereCast);
    }
    Contact cueContact(int ci) override {
        Contact c; BallState cb; if (!get(ci, cb)) return c;
        const int n = overlap(cb.pos, (float)(ballRadius + 2.0 * contactOffset), -1, 1 /* Ignore triggers */); void** el = elems();
        for (int i = 0; i < n && i < 24; ++i) {
            void* col = el[i]; if (!okObj(col)) continue; void* rb = callP(colRb, col); if (rb == bodies_[ci]) continue;
            if (tagIs(col, "Table")) continue;                         // the game excludes only tag "Table" (P2R)
            c.touched = true; const int bi = rb ? bodyIndex(rb) : -1; if (bi >= 0) { c.ballId = bi; c.edge = false; return c; } c.edge = true;
        }
        return c;
    }
    bool inPocket(int idx) override {
        BallState b; if (!get(idx, b)) return false;
        const int n = overlap(b.pos, (float)ballRadius, -1, 2 /* Collide: include triggers (P2T) */); void** el = elems();
        for (int i = 0; i < n && i < 24; ++i) { void* col = el[i]; if (!okObj(col)) continue; if (callB(colTrig, col) == 1 && tagIs(col, "PocketInside")) return true; }
        return false;
    }
    void park(int idx) override { BallState s; s.id = idx; s.pos = {0, -100, 0}; s.q[3] = 1; set(idx, s); }
    bool pos(int i, double& x, double& z) override { F3 p; if (!getF3(rbGetPos, bodies_[i], p)) return false; x = p.x; z = p.z; return true; }
    double speed(int i) override { F3 v; if (!getF3(rbGetVel, bodies_[i], v)) return 0; return std::sqrt((double)v.x * v.x + (double)v.z * v.z); }
    bool nearPocket(double x, double z) override {
        if (pockets_.empty()) return true;   // unknown pocket positions: never skip
        for (auto& p : pockets_) { const double dx = x - p.first, dz = z - p.second; if (dx * dx + dz * dz < pocketNear_ * pocketNear_) return true; }
        return false;
    }
    int bodyIndex(void* rb) const { for (size_t i = 0; i < bodies_.size(); ++i) if (bodies_[i] == rb) return (int)i; return -1; }
    bool tagIs(void* comp, const char* tag) {
        if (cmpTag.ok()) return strOf(callP(cmpTag, comp)) == tag;
        if (cmpGo.ok() && goTag.ok()) { void* go = callP(cmpGo, comp); return okObj(go) && strOf(callP(goTag, go)) == tag; }
        return false;
    }

private:
    std::vector<void*> bodies_, trs_; int cue_ = -1; void* arr_ = nullptr; bool constRead_ = false;
    std::vector<std::pair<double, double>> pockets_; double pocketNear_ = 0.3;
    void** elems() { return (void**)((char*)arr_ + 0x20); }   // Il2CppArray: header 0x18 (klass, monitor, bounds, max_length) -> first element @0x20
    int overlap(const Vec3& p, float r, int mask, int qti) {
        if (!phOverlap || !arr_) return 0;
        return ((int (*)(F3, float, void*, int, int, const Method*))fnptr(phOverlap))(F3{(float)p.x, (float)p.y, (float)p.z}, r, arr_, mask, qti, phOverlap);
    }
};

}  // namespace ppu
