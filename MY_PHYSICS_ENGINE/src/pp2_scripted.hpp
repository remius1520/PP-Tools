// PP2 scripted collision results — the game's own post-collision rules, decoded from libil2cpp (ARM64).  NOT wired into Sim::run yet (see docs/FINDINGS_v5.md: trigger timing + hitNormal producer still undecoded).
//   PhysicsSolver.BallToEdgeCollisionDir  0x5D3A8AC : v' = (v - 2 (v.n) n) * e_edge          (all 3 components, no tangential loss)
//   PhysicsSolver.BallToBallCollisionDir  0x5D3A594 : R  = -n_xz/|n_xz| * |sin(angle) * (|vin| - clamp(hitDist,0,|vin|))| * e_ball, y = 0   (n_xz kept only if |n_xz| > 1e-5)
//   e_edge = e_ball = 0.92f  (PhysicsSolver .cctor 0x5D3A974: 0x3F6B851F duplicated into static[0],static[4]);  min length constant 1e-5 (rodata 0x1319D60), 1e-15 (0x1318 2A8)
//   SolveCollisions 0x5D3D35C (called from PhysicsBuilder.Update only when NetworkManager.IsServer), per dequeued CollisionEvent {Ball A, Collider B, v1, hitNormal}:
//     B tagged Edge : if (spinActive || !A.isCue) A.velocity = BallToEdge(v1, hitNormal)          [cue ball without spin stays PhysX]
//     B tagged Ball : angle = acos(clamp(n.vhat/(|n||vhat|),-1,1)) in degrees, vhat = normalize(A.velocity AFTER PhysX) (fallback static vector if |v|<=1e-5; skipped if |n||vhat|<1e-15);
//                     R = BallToBall(B, v1, hitDist = 0 (movi d3,#0 at 0x5D3D904), angle, hitNormal);  B.velocity = R;
//                     if (spinActive || !A.isCue) A.velocity = A.velocity - R
#pragma once
#include <cmath>
#include <algorithm>
namespace pp2 { namespace scripted {
struct F3 { float x, y, z; };
constexpr float E_EDGE = 0.92f, E_BALL = 0.92f, MIN_LEN = 1e-5f;
inline F3 edgeDir(F3 v, F3 n, float e = E_EDGE) {
    const float d = (v.x * n.x + v.y * n.y + v.z * n.z) * -2.0f;
    return {(v.x + n.x * d) * e, (v.y + n.y * d) * e, (v.z + n.z * d) * e};
}
inline F3 ballDir(F3 vin, float hitDist, float angleDeg, F3 n, float e = E_BALL) {
    const float sp = std::sqrt(vin.x * vin.x + vin.y * vin.y + vin.z * vin.z);
    const float clampd = hitDist < sp ? hitDist : sp, c2 = hitDist < 0 ? 0.0f : clampd;
    const float mag = std::fabs(std::sin(angleDeg * 0.0174533f) * (sp - c2));
    const float l = std::sqrt(n.x * n.x + n.z * n.z); F3 d{0, 0, 0};
    if (l > MIN_LEN) { d.x = -n.x / l; d.z = -n.z / l; }
    return {d.x * mag * e, 0.0f, d.z * mag * e};
}
inline float angleDegBetween(F3 vAfterPhysX, F3 n, F3 fallbackDir = {0, 0, 0}) {
    float l = std::sqrt(vAfterPhysX.x * vAfterPhysX.x + vAfterPhysX.y * vAfterPhysX.y + vAfterPhysX.z * vAfterPhysX.z); F3 h = l > MIN_LEN ? F3{vAfterPhysX.x / l, vAfterPhysX.y / l, vAfterPhysX.z / l} : fallbackDir;
    const float hl2 = h.x * h.x + h.y * h.y + h.z * h.z, nl2 = n.x * n.x + n.y * n.y + n.z * n.z, den = std::sqrt(nl2 * hl2);
    if (den < 1e-15f) return 0.0f; float c = (n.x * h.x + n.y * h.y + n.z * h.z) / den; c = std::max(-1.0f, std::min(1.0f, c));
    return (float)(std::acos((double)c) * 57.2958);
}
}}  // namespace
