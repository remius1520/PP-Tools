#pragma once
#include <cmath>
#include <algorithm>
// Step27 - post-collision velocity hand-off, reconstructed from ARM64 (libil2cpp.so):
//   CueBall.ApplySpin                                       RVA 0x5DD83B4
//   HandlePhysicsCollisions.SetTargetBallAfterCollision     RVA 0x5D3B350
//   HandlePhysicsCollisions.SetCueDirectionAfterBallCollide RVA 0x5D3B964
//   CueBall.PredictCueRotationAngelAfterCollision           RVA 0x5DD7490
//
// Offline pure math only. Single precision on purpose (the binary uses s-registers).
// Evidence class per item: (A) read from disassembly, (C) runtime input left explicit,
// (U) Unity public reference algorithm, not read from this binary.
//
// KEY FINDING: ApplySpin computes no spin itself. It reads an already-solved result record from a
// SingeltonNetwork<?> instance (static slot image+0x7502608, type NOT resolved) and only
//   (1) sets the target ball's linearVelocity = normalize_xz(vec@E0)*speed@EC             (A)
//   (2) sets the cue ball's  linearVelocity = normalize_xz(vec@C8)*scalar@B8 (conditions) (A)
namespace pp2 { namespace postcol {

struct Vec3f { float x = 0, y = 0, z = 0; };

// (A) Float literals read directly from libil2cpp.so (part01+part02 merged, .rodata RVA == file offset):
constexpr float kMinDirLen   = 9.999999747378752e-06f; // image+0x1319D60 = 0x2737ACC5 -> 1e-5f
constexpr float kHighTarget  = 265.0f;                 // image+0x1318C5C = 0x43848000
constexpr float kSmoothTime  = 0.029999999329447746f;  // image+0x1319A50 = 0x3CF5C28F -> 0.03f

// (C) fallback: Vector3 static read at [class+0xB8] (x at +0, z at +8) when the length test fails.

// SetTargetBallAfterCollision, 0x5D3B5C0..0x5D3B624.  (A)
// len = sqrt(z*z + (x*x + y*y)).  len > minLen: dir = (x/len, z/len) (y only enters the length).
// otherwise dir = (fallback.x, fallback.z). velocity = (dirX*speed, 0, dirZ*speed); it is stored in
// ball+0x168/+0x16C/+0x170 and passed to Rigidbody(ball+0xE0).set_linearVelocity (y argument = 0).
struct TargetInput { float speed = 0, x = 0, y = 0, z = 0, minLen = kMinDirLen; Vec3f fallback; };
inline Vec3f targetBallVelocity(const TargetInput& in) {
    float a = in.x * in.x, b = in.y * in.y;
    float len = std::sqrt(in.z * in.z + (a + b));
    float dx, dz;
    if (len > in.minLen) { dx = in.x / len; dz = in.z / len; }
    else { dx = in.fallback.x; dz = in.fallback.z; }
    Vec3f v; v.x = dx * in.speed; v.y = 0.0f; v.z = dz * in.speed;
    return v;
}

// SetCueDirectionAfterBallCollide, 0x5D3B964.  (A)
// vx=[R+0xC8], vz=[R+0xD0] ([R+0xCC] is passed by ApplySpin but IGNORED), scalar=[R+0xB8].
// Early return (rigidbody untouched): collidedBall == null OR collidedBall.gameObject.CompareTag(staticTag).
// dir = (vx/len, 0, vz/len) if len > minLen else (0,0,0).
// s9  = scalar; if (this+0x24 >= scalar) s9 = SpinAppliedBool ? 0 : scalar   (NaN compare => skipped)
// collidedBall.CompareTag(CueBall.dx()) true : velocity = dir * scalar, body not reset
//                                        false: linear+angular velocity := static Vector3 [class+0xB8],
//                                               then velocity = dir * s9
struct CueInput {
    float vx = 0, vz = 0, scalar = 0, minLen = kMinDirLen;
    float zeroSpinThreshold = 0;    // HandlePhysicsCollisions+0x24
    bool spinApplied = false;       // SpinController.get_SpinAppliedBool (0x5DA1264)
    bool collidedBallIsNull = false;
    bool excludedTagHit = false;    // collidedBall.gameObject.CompareTag(excluded tag)
    bool bodyTagMatchesDx = false;  // collidedBall.CompareTag(CueBall.dx())
};
struct CueResult {
    bool changed = false;   // false = early return
    bool resetBody = false; // linear+angular velocity set to the static Vector3 first
    Vec3f velocity;
};
inline CueResult cueVelocityAfterBallCollide(const CueInput& in) {
    CueResult r;
    if (in.collidedBallIsNull || in.excludedTagHit) return r;
    float len = std::sqrt(in.vx * in.vx + in.vz * in.vz);
    float dx = 0, dz = 0;
    if (len > in.minLen) { dx = in.vx / len; dz = in.vz / len; }
    float s9 = in.scalar;
    if (in.zeroSpinThreshold >= in.scalar) s9 = in.spinApplied ? 0.0f : in.scalar;
    float speed;
    if (in.bodyTagMatchesDx) speed = in.scalar;
    else { speed = s9; r.resetBody = true; }
    r.changed = true;
    r.velocity.x = dx * speed; r.velocity.y = 0.0f * speed; r.velocity.z = dz * speed;
    return r;
}

// PredictCueRotationAngelAfterCollision 0x5DD7490.  (A) branch table, (C) highTarget.
// a = CueBall.intBallAngle (+0x1C4, unsigned compare).
//   a > 180: 180 (code 4)   a < 88: 95 (3)   88..89: 150 (4)   90: 180 (4)   91..92: 210 (4)
//   93..180: highTarget = float at image+0x1318C5C (code 2)
// code = int passed to SpinController.Inverse(SpinDirection) (0x5D9E4F8). The target feeds
// Mathf.SmoothDamp(this+0x254, target, ref this+0x258, smoothTime@0x1319A50, +inf, deltaTime).
// highTarget = kHighTarget = 265 (verified from the binary; matches legacy predict_core.hpp).
struct RotTarget { float target; int inverseCode; };
inline RotTarget predictCueRotationTarget(unsigned a, float highTarget) {
    if (a > 180u) return {180.0f, 4};
    if (a < 88u)  return {95.0f, 3};
    if (a < 90u)  return {150.0f, 4};
    if (a == 90u) return {180.0f, 4};
    if (a <= 92u) return {210.0f, 4};
    return {highTarget, 2};
}

// (U) Unity Mathf.SmoothDamp public reference implementation (0x6C16034 is the 6-argument overload).
inline float smoothDamp(float current, float target, float& vel, float smoothTime, float maxSpeed, float dt) {
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime, x = omega * dt;
    float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target, originalTo = target;
    float maxChange = maxSpeed * smoothTime;
    change = std::min(std::max(change, -maxChange), maxChange);
    target = current - change;
    float temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * e;
    float out = target + (change + temp) * e;
    if ((originalTo - current > 0.0f) == (out > originalTo)) { out = originalTo; vel = (out - originalTo) / dt; }
    return out;
}

}} // namespace pp2::postcol
