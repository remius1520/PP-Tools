// PP v92c Part B1 — STATE READING BY NAME.
//
// Every game field is resolved by NAME via il2cpp reflection
// (il2cpp_class_get_field_from_name + il2cpp_field_get_offset), with the
// dump.cs constant as a fieldOff() fallback.  A REPORT line is emitted per
// field: OK/MISSING, the method used (name / property / offset), and why.
// If no method works for a field, drawing is disabled and the report says so.
//
// Names verified from MY_GAME/dump.cs:
//   Ball           : ballId(get_BallId), isCue(get_IsCue), insidePocket,
//                    InTube(get_InTube via BallService.inTube),
//                    body(get_Body -> Rigidbody.position)
//   CueBall        : scaledDirection(0x1B0), <IsAiming>k__BackingField(0x26D)
//   GameManager    : isHit(get_isHit), balls list (GM_balls)
//   PhysicsBuilder : forceDirection(PB_forceDir), targetVel(PB_targetVel),
//                    levelMax(PB_levelMax)
//   SpinController : get_SpinFactor
#pragma once
#include "il2cpp.hpp"
#include "unityhost.hpp"
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

namespace ppu {

// Field read result: one line per field for the B1 report.
struct FieldRes {
    const char* cls;
    const char* field;
    const char* method;   // "name", "property", or "offset"
    bool ok;
    const char* why;
};

// Resolve a field offset by NAME first, fallback to the dump.cs constant.
// Returns true when name-based resolution succeeded (sets *out).
static bool fieldOffByName(Klass* k, const char* name, int fallback, int& out) {
    if (!k || !il2cpp_class_get_field_from_name || !il2cpp_field_get_offset) {
        out = fallback; return false;
    }
    void* f = il2cpp_class_get_field_from_name(k, name);
    if (f) {
        int o = (int)il2cpp_field_get_offset(f);
        if (o > 0 && o < 0x2000) { out = o; return true; }   // NAME resolved
    }
    out = fallback; return false;   // offset fallback
}

// ---------- per-field OK/MISSING report for B1 ----------
struct StateReport {
    std::string lines;
    int okCount = 0, missCount = 0;
    void add(const char* cls, const char* field, const char* method,
             bool ok, const char* why) {
        char b[220];
        snprintf(b, sizeof b, "%-8s %-22s %-22s %-10s %s\n",
                 ok ? "OK" : "MISSING", cls, field, method, why);
        lines += b;
        if (ok) ++okCount; else ++missCount;
    }
    void result() {
        char b[120];
        snprintf(b, sizeof b, "RESULT: %d OK, %d MISSING (of %d)\n",
                 okCount, missCount, okCount + missCount);
        lines += b;
    }
    const std::string& str() const { return lines; }
};

// Snapshot of all readable per-ball state.
struct BallStateEx {
    int idx = -1;
    int ballId = -1;
    bool isCue = false;
    bool insidePocket = false;
    bool inTube = false;
    double x = 0, z = 0;
    bool ok = false;            // all critical fields resolved
};

// Read every ball (id, position, type solid/stripe/cue, InTube) by NAME.
static int readBallsByName(Klass* kBall, Klass* kRb, void* ballList,
                           StateReport& rep, std::vector<BallStateEx>& out) {
    out.clear();
    int nTotal = 0;
    if (!ballList) {
        rep.add("GameManager", "balls", "offset", false, "ball list null");
        return 0;
    }
    void* items = rdp<void*>(ballList, off::L_items);
    int n = rdp<int>(ballList, off::L_size);
    nTotal = n;
    if (!plausible(items) || n <= 0 || n >= 64) {
        rep.add("GameManager", "balls", "offset", false, "list items implausible");
        return 0;
    }

    // Resolve Ball field offsets by NAME once.
    int oId = off::PB_forceDir;           bool idName  = fieldOffByName(kBall, "ballId", off::PB_forceDir, oId);
    int oIsCue = off::B_isCue;            bool cueName = fieldOffByName(kBall, "isCue", off::B_isCue, oIsCue);
    int oInPocket = off::B_insidePocket;  bool pkName  = fieldOffByName(kBall, "insidePocket", off::B_insidePocket, oInPocket);
    int oBody = off::PB_forceDir;         bool bodyName = fieldOffByName(kBall, "body", off::PB_forceDir, oBody);

    rep.add("Ball", "BallId", idName ? "name" : "offset", idName, idName ? "get_BallId resolved" : "get_BallId not found");
    rep.add("Ball", "isCue", cueName ? "name" : "offset", cueName, cueName ? "get_IsCue field resolved" : "get_IsCue field not found");
    rep.add("Ball", "insidePocket", pkName ? "name" : "offset", pkName, pkName ? "insidePocket field resolved" : "insidePocket field not found");
    rep.add("Ball", "body", bodyName ? "name" : "offset", bodyName, bodyName ? "get_Body field resolved" : "get_Body field not found");

    int cueIdx = -1;
    for (int i = 0; i < n; ++i) {
        void* b = rdp<void*>(items, off::ARR_data + 8 * i);
        if (!alive(b)) continue;
        BallStateEx bs; bs.idx = i;

        // id
        if (idName) bs.ballId = rdp<int>(b, oId); else bs.ballId = i;
        // type
        bs.isCue = cueName ? rdp<bool>(b, oIsCue) : rdp<bool>(b, off::B_isCue);
        bs.insidePocket = pkName ? rdp<bool>(b, oInPocket) : rdp<bool>(b, off::B_insidePocket);
        // position via Rigidbody (name-resolved body)
        void* rb = bodyName ? rdp<void*>(b, oBody) : nullptr;
        if (rb && kRb) {
            F3 p{}; bool hasPos = false;
            // Rigidbody.get_position via name (Acc) with offset fallback not needed: use unityhost Acc
            Acc a = findAcc(kRb, {"get_position"}, 0,
                            {"UnityEngine.Rigidbody::get_position_Injected"});
            if (a.ok()) hasPos = getF3(a, rb, p);
            if (hasPos) { bs.x = p.x; bs.z = p.z; bs.ok = true; }
        }
        // InTube via BallService.ballService field (NetworkVariable<bool>) by name
        int oSvc = off::PB_forceDir; bool svcName = fieldOffByName(kBall, "ballService", off::PB_forceDir, oSvc);
        if (svcName) {
            void* svc = rdp<void*>(b, oSvc);
            if (plausible(svc)) {
                Klass* kSvc = (Klass*)rdp<void*>(svc, 0);
                int oTube = off::GM_balls; bool tubeName = fieldOffByName(kSvc, "inTube", off::GM_balls, oTube);
                if (tubeName) bs.inTube = rdp<bool>(svc, oTube);
                rep.add("BallService", "inTube", tubeName ? "name" : "offset", tubeName,
                        tubeName ? "NetworkVariable<bool>.inTube resolved" : "inTube not found");
            } else {
                rep.add("BallService", "inTube", "offset", false, "ballService null");
            }
        } else {
            rep.add("BallService", "inTube", "offset", false, "ballService field not found");
        }
        out.push_back(bs);
        if (bs.isCue) cueIdx = i;
    }
    rep.add("GameManager", "balls", "offset", n > 0, "n balls listed");
    return cueIdx;
}

// Read shot/table state by NAME: aim direction, power, spin, cue item,
// isAiming, isHit.
struct ShotStateEx {
    bool pulling = false;
    double F = 0, fx = 0, fz = 0;
    double dirX = 0, dirZ = 0;
    double spin = -1;
    bool isAiming = false;
    bool isHit = false;
    bool ok = false;
};

static bool readShotStateByName(Klass* kCueBall, Klass* kPb, Klass* kSpin,
                                Klass* kGM, void* cb, void* pb, void* gm,
                                StateReport& rep, ShotStateEx& out) {
    out = ShotStateEx{};
    // aim direction: CueBall.scaledDirection (name), fallback offset 0x1B0
    int oSD = off::CB_scaledDirection; bool sdName = fieldOffByName(kCueBall, "scaledDirection", off::CB_scaledDirection, oSD);
    rep.add("CueBall", "scaledDirection", sdName ? "name" : "offset", sdName,
            sdName ? "scaledDirection field resolved" : "scaledDirection not found");
    Vec3 dS{}; bool hasDir = false;
    if (sdName && cb) { dS = rdp<Vec3>(cb, oSD); hasDir = true; }
    else if (cb) { dS = rdp<Vec3>(cb, off::CB_scaledDirection); hasDir = true; }
    // isAiming: CueBall.<IsAiming>k__BackingField (name), fallback offset 0x26D
    int oAim = off::CB_isAiming; bool aimName = fieldOffByName(kCueBall, "<IsAiming>k__BackingField", off::CB_isAiming, oAim);
    rep.add("CueBall", "IsAiming", aimName ? "name" : "offset", aimName,
            aimName ? "<IsAiming>k__BackingField resolved" : "<IsAiming> not found");
    out.isAiming = aimName ? rdp<bool>(cb, oAim) : rdp<bool>(cb, off::CB_isAiming);
    // isHit: GameManager.get_isHit (property -> name), fallback offset 0xAC
    int oHit = off::GM_isHit; bool hitName = fieldOffByName(kGM, "isHit", off::GM_isHit, oHit);
    rep.add("GameManager", "isHit", hitName ? "property" : "offset", hitName,
            hitName ? "get_isHit property resolved" : "isHit not found");
    out.isHit = hitName ? rdp<bool>(gm, oHit) : rdp<bool>(gm, off::GM_isHit);
    // power: PhysicsBuilder.forceDirection (name), fallback PB_forceDir
    int oFd = off::PB_forceDir; bool fdName = fieldOffByName(kPb, "forceDirection", off::PB_forceDir, oFd);
    rep.add("PhysicsBuilder", "forceDirection", fdName ? "name" : "offset", fdName,
            fdName ? "forceDirection field resolved" : "forceDirection not found");
    if (pb && fdName) { out.fx = (double)rdp<float>(pb, oFd); out.fz = (double)rdp<float>(pb, oFd + 8); }
    else if (pb) { out.fx = (double)rdp<float>(pb, off::PB_forceDir); out.fz = (double)rdp<float>(pb, off::PB_forceDir + 8); }
    out.F = std::sqrt(out.fx * out.fx + out.fz * out.fz);
    // spin: SpinController.get_SpinFactor (name -> property), fallback not available
    bool spinName = false;
    if (kSpin) {
        const Method* m = findMethod(kSpin, "get_SpinFactor", 0);
        if (m && fnptr(m)) { out.spin = (double)((float(*)(void*, const Method*))fnptr(m))(nullptr, m); spinName = true; }
    }
    rep.add("SpinController", "SpinFactor", spinName ? "property" : "MISSING", spinName,
            spinName ? "get_SpinFactor resolved" : "get_SpinFactor not found");
    out.dirX = dS.x; out.dirZ = dS.z;
    out.pulling = out.F > 0.02;
    out.ok = sdName && aimName && hitName && fdName && spinName;
    return out.ok;
}

}  // namespace ppu