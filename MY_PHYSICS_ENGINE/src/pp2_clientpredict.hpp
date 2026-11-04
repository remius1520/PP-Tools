// P2H - client-side direction predictors, decoded from libil2cpp.so (ARM64) in this session.
// They fill the ShotData fields that the server (ApplySpin) later applies unchanged:
//   CueBall.collidedBallForceDirection  (+0x22C) <- OnCastBall 0x5DD7FDC  -> PhysicsBuilder.PredictTargetBallAfterCollision 0x5D3CC58
//   CueBall.forceDirectionAfterCollided (+0x220) <- OnHandleCueDirection 0x5DD6EE0
//        Ball collider ("Ball", a.nn() = decoded string)  -> PhysicsBuilder.PredictCueDirAfterBallCollision 0x5D420A8
//        other (Edge)                                      -> PhysicsBuilder.PredictCueDirAfterEdgeCollision 0x5D43858
// Single precision on purpose (the binary uses s-registers). Evidence: (A) read from disassembly.
// (Obfuscated duplicates of the same logic exist: md 0x5DDAFC4, imm 0x5DD8CE4, emx 0x5DD9400, nrv 0x5DD9D40.)
#pragma once
#include <cmath>

namespace pp2 { namespace cp {

struct V3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Ray { V3 origin, dir; };

constexpr float kMinLen   = 1e-5f;                 // rodata 0x1319D60  (A)
constexpr float kTinyLen  = 1e-15f;                // rodata 0x13182A8 (A)
constexpr float kRad2Deg  = 57.2958f;              // rodata 0x13190E4  (A)
constexpr float kDeg2Rad  = 0.0174533f;            // rodata 0x1319E1C  (A)
constexpr float kMinus90Rad = -1.5707963705062866f; // rodata 0x131DCA8 : Euler(0,-pi/2,0) used for Rn (A)

// Quaternion.Euler(0, rad, 0) (Internal_FromEulerRad with x=z=0).
inline Quat eulerY(float rad) { Quat q; q.y = std::sin(rad * 0.5f); q.w = std::cos(rad * 0.5f); return q; }

// Quaternion.Internal_ToEulerRad .y (P2R, libunity.so): icall 0x550EB4 -> normalise q -> 0xDD710C(q, order 4).  Non-gimbal branch (|yz - xw| < 0.4999995f = 0x3EFFFFDE):
//   y = atan2( 2(xz + yw), ww + zz - xx - yy )          (the 2nd of the three function-pointer calls at 0xDD74CC..; atan2 identity inferred from the argument shape, GOT slot not resolved)
// For a pure-Y rotation (x = z = 0) the gimbal test is 0 < 0.4999995, so y = atan2(2yw, ww - yy) = the angle itself, wrapped to (-pi, pi]  (NOT 0 at 180 deg).  (A)
inline float toEulerYRad(Quat q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (l >= -0.0f) { q.x /= l; q.y /= l; q.z /= l; q.w /= l; }
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, ww = q.w * q.w, xz = q.x * q.z, yw = q.y * q.w;
    return std::atan2(2 * (xz + yw), ww + zz - xx - yy);
}
// Euler(...).y after Unity's MakePositive (degrees; public reference: wrap into [-0.0001, 360-0.0001]).
inline float eulerYDegPositive(Quat q) {
    float d = toEulerYRad(q) * kRad2Deg; const float nf = -0.0001f, pf = 360.0f + nf;
    while (d < nf) d += 360.0f; while (d > pf) d -= 360.0f; return d;
}

// Unity Quaternion * Vector3 (public reference formula, op_Multiply 0x6C143B4).  (U)
inline V3 rotate(const Quat& q, const V3& v) {
    float x2 = q.x * 2, y2 = q.y * 2, z2 = q.z * 2;
    float xx = q.x * x2, yy = q.y * y2, zz = q.z * z2;
    float xy = q.x * y2, xz = q.x * z2, yz = q.y * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    return { (1 - (yy + zz)) * v.x + (xy - wz) * v.y + (xz + wy) * v.z,
             (xy + wz) * v.x + (1 - (xx + zz)) * v.y + (yz - wx) * v.z,
             (xz - wy) * v.x + (yz + wx) * v.y + (1 - (xx + yy)) * v.z };
}

// PredictTargetBallAfterCollision(hitPoint, radius, hitNormal) 0x5D3CC58.  (A)
//   d = normalize_xz(n) (zero if |n_xz| <= 1e-5);  origin = (hit.x - d.x*r, 0, hit.z - d.z*r);
//   direction = (-d.x, 0, -d.z)/|d|  (|d| = 1 normally, 0 -> direction 0).
// OnCastBall stores direction at CueBall+0x22C and the Ray (origin,dir) at +0x238.
inline Ray predictTargetBallAfterCollision(V3 hit, float radius, V3 n) {
    float len = std::sqrt(n.x * n.x + n.z * n.z);
    float dx = 0, dz = 0;
    if (len > kMinLen) { dx = n.x / len; dz = n.z / len; }
    float l2 = std::sqrt(dx * dx + dz * dz);
    Ray r;
    r.origin = { hit.x - dx * radius, 0, hit.z - dz * radius };
    if (l2 > kMinLen) r.dir = { -dx / l2, 0, -dz / l2 };
    return r;
}

// OnHandleCueDirection 0x5DD6EE0, part 1.  (A)
//   Rn = Euler(0,-pi/2,0) * hitNormal;  c = -(u . Rn) / sqrt(|u|^2 |Rn|^2) (only if that root >= 1e-15), clamp [-1,1]
//   ballAngle = acos(c) * 57.2958 -> CueBall+0x1BC;   intBallAngle (+0x1C4) = RoundToInt(ballAngle) (banker's).
// If the root < 1e-15 the binary leaves s14 = |u|^2 as "angle" (degenerate; returned as is).
inline float ballAngleDeg(V3 u, V3 hitNormal) {
    V3 rn = rotate(eulerY(kMinus90Rad), hitNormal);
    float uu = u.x * u.x + u.y * u.y + u.z * u.z, rr = rn.x * rn.x + rn.y * rn.y + rn.z * rn.z;
    float den = std::sqrt(uu * rr);
    if (den < kTinyLen) return uu;
    float c = -(u.x * rn.x + u.y * rn.y + u.z * rn.z) / den;
    c = c > 1.f ? 1.f : (c < -1.f ? -1.f : c);
    return (float)std::acos((double)c) * kRad2Deg;
}
inline int roundToIntEven(float a) { return (int)std::nearbyint(a); }

// PredictCueDirAfterBallCollision(classic, spin, n) 0x5D420A8.  (A)
// uses `spin` iff Euler(spin).y (after MakePositive) != 0, i.e. SpinAngelDeg != 0; otherwise `classic`; result = q * n.
// `spinAngleDeg` is SpinController.SpinAngelDeg (spinAngelDeg NetworkVariable, field angleDeg +0xC0; 0 after OnResetSpin/ResetSpinValues).
inline V3 predictCueDirAfterBallCollision(const Quat& classic, float spinAngleDeg, V3 n) {
    Quat spin = eulerY(spinAngleDeg * kDeg2Rad);
    const Quat& q = (spinAngleDeg != 0.f) ? spin : classic;
    return rotate(q, n);
}

// PredictCueDirAfterEdgeCollision(spin, scaledDirection v, n) 0x5D43858.  (A)
//   d = v . n (all 3 components);  w = (v.x - 2d n.x, 0, v.z - 2d n.z);  result = spin * w   (spin ALWAYS applied here)
inline V3 predictCueDirAfterEdgeCollision(float spinAngleDeg, V3 v, V3 n) {
    float d = v.x * n.x + v.y * n.y + v.z * n.z, k = d * -2.f;
    V3 w = { v.x + n.x * k, 0.f, v.z + n.z * k };
    return rotate(eulerY(spinAngleDeg * kDeg2Rad), w);
}

// OnHandleCueDirection part 2: CueBall.forceDirectionAfterCollided (+0x220) = normalize_xyz(result) when |result| > 1e-5,
// otherwise it keeps the register value = Rn (ball case; unnormalised) -> `fallback`.
inline V3 forceDirectionAfterCollided(V3 predicted, V3 fallback) {
    float len = std::sqrt(predicted.z * predicted.z + (predicted.x * predicted.x + predicted.y * predicted.y));
    if (len > kMinLen) return { predicted.x / len, predicted.y / len, predicted.z / len };
    return fallback;
}

}} // namespace pp2::cp
