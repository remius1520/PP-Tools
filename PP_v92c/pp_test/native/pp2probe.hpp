// PP2 probe (v87): dry OK/MISSING report of every class / method / field the PP2 simulator (PP2_SIM_V30_P2Z3) needs from the running game.
// Names only: it NEVER calls a game method, never reads game state, never changes anything.  Safe to run any time after il2cpp is up.
// Output = text (one line per item) + a RESULT line.  The payload writes it to pp2_probe.txt in the game's cache dir; the app copies it to Download.
#pragma once
#include "il2cpp.hpp"
#include <string>
#include <vector>
#include <cstdio>

namespace ppu {
using namespace il2;

struct ProbeItem { char kind; const char* ns; const char* cls; const char* member; int argc; const char* why; };   // kind: 'M' method, 'F' field, 'C' class only
static const ProbeItem kProbe[] = {
    // Unity (live predictor: snapshot / simulate / restore)
    {'M', "UnityEngine", "Rigidbody", "get_position", 0, "snapshot"}, {'M', "UnityEngine", "Rigidbody", "set_position", 1, "restore"},
    {'M', "UnityEngine", "Rigidbody", "get_rotation", 0, "snapshot"}, {'M', "UnityEngine", "Rigidbody", "set_rotation", 1, "restore"},
    {'M', "UnityEngine", "Rigidbody", "get_linearVelocity", 0, "snapshot"}, {'M', "UnityEngine", "Rigidbody", "set_linearVelocity", 1, "restore / shot"},
    {'M', "UnityEngine", "Rigidbody", "get_angularVelocity", 0, "snapshot"}, {'M', "UnityEngine", "Rigidbody", "set_angularVelocity", 1, "restore / shot"},
    {'M', "UnityEngine", "Rigidbody", "Sleep", 0, "re-sleep"}, {'M', "UnityEngine", "Rigidbody", "IsSleeping", 0, "snapshot"}, {'M', "UnityEngine", "Rigidbody", "WakeUp", 0, "restore"},
    {'M', "UnityEngine", "Physics", "Simulate", 1, "tick"}, {'M', "UnityEngine", "Physics", "get_simulationMode", 0, "must be Script"},
    {'M', "UnityEngine", "Physics", "OverlapSphereNonAlloc", 5, "contact / pocket"}, {'M', "UnityEngine", "Physics", "SphereCast", 6, "cue cast"},
    {'M', "UnityEngine", "Collider", "get_attachedRigidbody", 0, "contact"}, {'M', "UnityEngine", "Collider", "get_isTrigger", 0, "pocket"},
    {'M', "UnityEngine", "Component", "get_tag", 0, "Table / PocketInside / Edge"},
    // game (names from dump.cs, P2Z / P2Q / P2R)
    {'M', "PoolLiveTourMax.Managers", "GameManager", "get_Instance", 0, "balls list"}, {'M', "PoolLiveTourMax.Managers", "GameManager", "get_Balls", 0, "balls list"},
    {'F', "PoolLiveTourMax.Managers", "GameManager", "<isHit>k__BackingField", 0, "shot in progress flag"},
    {'M', "PoolLiveTourMax.Gameplay.Balls", "Ball", "get_Body", 0, "rigidbody of a ball"},
    {'C', "PoolLiveTourMax.Gameplay.Balls", "CueBall", "", 0, "cue ball class"},
    {'F', "PoolLiveTourMax.Gameplay.Balls", "CueBall", "forceDirectionAfterCollided", 0, "prediction fields"}, {'F', "PoolLiveTourMax.Gameplay.Balls", "CueBall", "collidedBallForceDirection", 0, "prediction fields"},
    {'F', "PoolLiveTourMax.Gameplay.Balls", "CueBall", "collidedBall", 0, "prediction fields"},
    {'M', "PoolLiveTourMax.Physic.Handlers", "GamePhysics", "get_Instance", 0, "singleton"}, {'F', "PoolLiveTourMax.Physic.Handlers", "GamePhysics", "physicsBuilder", 0, "shot data"},
    {'F', "PoolLiveTourMax.Physic.Handlers", "PhysicsBuilder", "forceDirection", 0, "shot data"}, {'F', "PoolLiveTourMax.Physic.Handlers", "PhysicsBuilder", "targetBallFinalVelocity", 0, "shot data"},
    {'M', "PoolLiveTourMax.Gameplay.Shot", "SpinController", "get_Instance", 0, "spin"}, {'M', "PoolLiveTourMax.Gameplay.Shot", "SpinController", "get_SpinFactor", 0, "spin"},
    // P2Z3: end of shot (class found by NAME in any namespace: the namespace was not recorded)
    {'M', "*", "GameManagerService", "OnPingBallVelocity", -1, "end of shot (P2Z3)"}, {'M', "*", "GameManagerService", "BallInAction", -1, "end of shot (P2Z3)"},
    {'M', "*", "ServerTracker", "SleepBalls", -1, "end of shot (P2Z3)"},
    {'M', "PoolLiveTourMax.Gameplay.Balls", "Ball", "IsMoving", -1, "stop test (P2Z3)"}, {'F', "PoolLiveTourMax.Gameplay.Balls", "Ball", "sleepThreshold", 0, "0.002 (P2Z3)"},
};

// class by namespace+name; ns "*" = any namespace (scans every image; needs the optional il2cpp_image_get_class* exports)
inline Klass* probeClass(const char* ns, const char* name, bool& scanned) {
    scanned = false;
    if (strcmp(ns, "*")) return findClass(ns, name);
    if (!il2cpp_image_get_class_count || !il2cpp_image_get_class || !il2cpp_class_get_name) return nullptr;
    scanned = true; size_t n = 0; Assembly** as = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; as && i < n; ++i) { const Image* im = il2cpp_assembly_get_image(as[i]); const size_t cc = il2cpp_image_get_class_count(im);
        for (size_t c = 0; c < cc; ++c) { const Klass* k = il2cpp_image_get_class(im, c); const char* cn = k ? il2cpp_class_get_name(k) : nullptr; if (cn && !strcmp(cn, name)) return (Klass*)k; } }
    return nullptr;
}

// returns the report; okCount / missCount filled.  Dry: only reflection lookups.
inline std::string probePP2(int& okCount, int& missCount) {
    std::string r; okCount = missCount = 0; char b[256];
    for (const ProbeItem& it : kProbe) {
        bool scanned = false; Klass* k = probeClass(it.ns, it.cls, scanned); bool ok = false; const char* extra = "";
        if (!k) extra = scanned || !strcmp(it.ns, "*") ? " (class not found)" : " (class not found)";
        else if (it.kind == 'C') ok = true;
        else if (it.kind == 'M') { ok = il2cpp_class_get_method_from_name(k, it.member, it.argc) != nullptr; }
        else if (it.kind == 'F') { if (!il2cpp_class_get_field_from_name) extra = " (il2cpp_class_get_field_from_name export absent)"; else ok = il2cpp_class_get_field_from_name(k, it.member) != nullptr; }
        if (ok) ++okCount; else ++missCount;
        const char* kindName = it.kind == 'M' ? "method" : it.kind == 'F' ? "field " : "class ";
        if (it.kind == 'C') snprintf(b, sizeof b, "%-8s %s %s.%s  [%s]%s\n", ok ? "OK" : "MISSING", kindName, it.ns, it.cls, it.why, extra);
        else if (it.kind == 'M' && it.argc >= 0) snprintf(b, sizeof b, "%-8s %s %s.%s/%d  [%s]%s\n", ok ? "OK" : "MISSING", kindName, it.cls, it.member, it.argc, it.why, extra);
        else snprintf(b, sizeof b, "%-8s %s %s.%s  [%s]%s\n", ok ? "OK" : "MISSING", kindName, it.cls, it.member, it.why, extra);
        r += b;
    }
    snprintf(b, sizeof b, "RESULT: %d OK, %d MISSING (of %d)\n", okCount, missCount, okCount + missCount); r += b;
    return r;
}

}  // namespace ppu
