// ServerTracker (PoolLiveTourMax.Gameplay.Server) — serialized tail + step logic, decoded for band 4 (P2Q).
//
// Sources:
//   * level5 scene, MonoBehaviour PathID 84 (script PPtr fileID 1 / pathID 443 = MonoScript "ServerTracker"), 160 bytes, no TypeTree.
//     The last 40 bytes are the serialized fields minXpoint..isSolving (order = field-name order in global-metadata.dat).
//   * libil2cpp.so: the managed field offsets/types the code actually uses CONFIRM that mapping:
//       +0x140 float  (ldr s15)    substeps              <- serialized 7.0f
//       +0x144 float  (fcmp)       stopSimulateThreshold <- serialized 0.002f
//       +0x148 int32  (str w8)     solvecount            <- serialized 8
//       +0x14C bool   (ldrb/strb)  isSolving             <- serialized 0
//     component-relative offset + 0xB0 == managed offset for all four (and for the six floats before them), i.e. no gap/reorder.
//   * OnServerSimulate 0x5DB856C (live; called by FixedUpdate 0x5DB99F4 via tail-b). 0x5DB6674 ("frq") is a byte-for-byte
//     duplicate with no bl/b caller and no pointer except its MethodInfo slot => dead copy.
//
// What OnServerSimulate does (see docs/FINDINGS_P2Q.md):
//   normal path            : if !isSolving: Physics.Simulate(fixedDeltaTime) ONCE.
//   "special" path (onSimulate +0x110 && !InTube && CueBall+0x10B==0 && CueBall+0x26C==0):
//       cueRb.linearVelocity = (start + vec) - cueRb.position   (every FixedUpdate)
//       if (hitFlag +0x108)  : if substeps<=0 -> nothing; else do { Simulate(fixedDelta / substeps) } while (substeps > i++)
//       else                 : if |(start+vec) - pos| < stopSimulateThreshold -> rb.position = target, rb.linearVelocity = 0, onSimulate=false
//                              then Simulate(fixedDelta) once.
//   hitFlag +0x108 = result of CueBall method 0x5DD6BB8 = Physics.SphereCast from the cue-ball position, radius = CueBall+0xB4 + 2*Physics.defaultContactOffset,
//                    direction = normalize(shot vector) (|v| <= 1e-5 -> fallback static vector), maxDistance = |shot vector|, layerMask = int at (singleton)+0xF8.
//   "is cue ball moving" predicate (4 copies, 0x5DB79D4 ...): CueBall exists && !InTube && |rb.linearVelocity| (3D, float sqrt) > stopSimulateThreshold (strict >).
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace pp2 { namespace st {

struct Tail {
    float minXpoint = 0, maxXpoint = 0, minZpoint = 0, maxZpoint = 0, minXRack = 0, maxXRack = 0;
    float substeps = 0;                  // managed +0x140
    float stopSimulateThreshold = 0;     // managed +0x144
    int32_t solvecount = 0;              // managed +0x148
    bool isSolving = false;              // managed +0x14C
};

constexpr int kTailBytes = 40;           // component bytes 120..159 (component-relative); managed offset = rel + 0xB0 for the first field
constexpr int kManagedFirstField = 0x128;

// level5 / PathID 84, component bytes 120..159 (verbatim, little endian)
constexpr uint8_t kLevel5Tail[kTailBytes] = {
    0xa6,0x9b,0xc4,0xba, 0xa6,0x9b,0xc4,0x3a, 0xa6,0x9b,0xc4,0xba, 0xa6,0x9b,0xc4,0x3a,
    0x29,0x5c,0x8f,0xbd, 0x29,0x5c,0x8f,0x3d, 0x00,0x00,0xe0,0x40, 0x6f,0x12,0x03,0x3b,
    0x08,0x00,0x00,0x00, 0x00,0x00,0x00,0x00 };

inline Tail parseTail(const uint8_t* p) {
    Tail t; float f[8]; memcpy(f, p, 32); int32_t i[2]; memcpy(i, p + 32, 8);
    t.minXpoint = f[0]; t.maxXpoint = f[1]; t.minZpoint = f[2]; t.maxZpoint = f[3]; t.minXRack = f[4]; t.maxXRack = f[5];
    t.substeps = f[6]; t.stopSimulateThreshold = f[7]; t.solvecount = i[0]; t.isSolving = (i[1] & 0xFF) != 0;
    return t;
}

// Number of Physics.Simulate calls in the flag-true loop (0x5DB8798..0x5DB87AC):
//   if (substeps <= 0) return;  i = 1;  do { Simulate(fixedDelta / substeps); s = (float)i; i++; } while (substeps > s);
inline int substepCount(float substeps) {
    if (!(substeps > 0.0f)) return 0;
    int i = 1, n = 0; do { ++n; float s = (float)i; ++i; if (!(substeps > s)) break; } while (n < 100000); return n;
}
// fdiv s8, s8, s15 : float32 division of Time.fixedDeltaTime (float) by substeps
inline float substepDt(float fixedDeltaTime, float substeps) { return fixedDeltaTime / substeps; }

}}  // namespace
