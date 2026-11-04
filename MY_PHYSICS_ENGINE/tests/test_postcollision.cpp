#include "../src/pp2_postcollision.hpp"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace pp2::postcol;
static int n = 0, fail = 0;
#define CHECK(c, m) do { ++n; if (!(c)) { ++fail; std::printf("FAIL %s line %d\n", m, __LINE__); } } while (0)
static bool near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
int main() {
    {   // target ball: y enters the length, is dropped from the result
        TargetInput i; i.speed = 2.0f; i.x = 3.0f; i.y = 4.0f; i.z = 12.0f; i.minLen = 0.001f;
        Vec3f v = targetBallVelocity(i);
        CHECK(near(v.x, 2.0f * 3.0f / 13.0f) && near(v.z, 2.0f * 12.0f / 13.0f), "target dir normalized by 3D length");
        CHECK(v.y == 0.0f, "target velocity y is zero");
        i.x = 0; i.y = 0; i.z = 0; i.fallback.x = 1.0f; i.fallback.z = -1.0f;
        v = targetBallVelocity(i);
        CHECK(near(v.x, 2.0f) && near(v.z, -2.0f), "short vector uses explicit fallback direction");
        i.minLen = 13.0f; i.x = 3; i.y = 4; i.z = 12;
        CHECK(near(targetBallVelocity(i).x, 2.0f), "length == minLen is not greater -> fallback (strict compare)");
    }
    {   // cue ball
        CueInput c; c.vx = 3.0f; c.vz = 4.0f; c.scalar = 10.0f; c.minLen = 0.001f;
        c.zeroSpinThreshold = 1.0f; c.spinApplied = true; c.bodyTagMatchesDx = false;
        CueResult r = cueVelocityAfterBallCollide(c);
        CHECK(r.changed && r.resetBody, "non-dx body is reset first");
        CHECK(near(r.velocity.x, 6.0f) && near(r.velocity.z, 8.0f) && r.velocity.y == 0.0f, "scalar above threshold: full scalar");
        c.zeroSpinThreshold = 10.0f;     // threshold >= scalar and spin applied -> 0
        r = cueVelocityAfterBallCollide(c);
        CHECK(r.velocity.x == 0.0f && r.velocity.z == 0.0f, "threshold >= scalar with spin applied zeroes speed");
        c.spinApplied = false;
        r = cueVelocityAfterBallCollide(c);
        CHECK(near(r.velocity.x, 6.0f), "threshold >= scalar without spin keeps scalar");
        c.spinApplied = true; c.bodyTagMatchesDx = true;
        r = cueVelocityAfterBallCollide(c);
        CHECK(!r.resetBody && near(r.velocity.x, 6.0f), "dx-tag branch ignores the zeroing and does not reset the body");
        c.collidedBallIsNull = true;
        CHECK(!cueVelocityAfterBallCollide(c).changed, "null collided ball -> early return");
        c.collidedBallIsNull = false; c.excludedTagHit = true;
        CHECK(!cueVelocityAfterBallCollide(c).changed, "excluded tag -> early return");
        c.excludedTagHit = false; c.vx = 0; c.vz = 0;
        r = cueVelocityAfterBallCollide(c);
        CHECK(r.changed && r.velocity.x == 0.0f && r.velocity.z == 0.0f, "zero direction gives zero velocity");
        c.vx = 3; c.vz = 4; c.zeroSpinThreshold = std::numeric_limits<float>::quiet_NaN(); c.bodyTagMatchesDx = false;
        r = cueVelocityAfterBallCollide(c);
        CHECK(near(r.velocity.x, 6.0f), "NaN threshold does not trigger the zeroing branch");
    }
    {   // rotation target table
        CHECK(predictCueRotationTarget(181, 7).target == 180.0f && predictCueRotationTarget(181, 7).inverseCode == 4, "a>180");
        CHECK(predictCueRotationTarget(87, 7).target == 95.0f && predictCueRotationTarget(87, 7).inverseCode == 3, "a<88");
        CHECK(predictCueRotationTarget(88, 7).target == 150.0f && predictCueRotationTarget(89, 7).target == 150.0f, "88..89");
        CHECK(predictCueRotationTarget(90, 7).target == 180.0f, "a==90");
        CHECK(predictCueRotationTarget(91, 7).target == 210.0f && predictCueRotationTarget(92, 7).target == 210.0f, "91..92");
        RotTarget t = predictCueRotationTarget(93, 7.0f);
        CHECK(t.target == 7.0f && t.inverseCode == 2, "93..180 uses explicit highTarget, code 2");
        CHECK(predictCueRotationTarget(180, 7.0f).target == 7.0f, "180 itself is in the high band");
    }
    {   // constants read from the binary
        CHECK(kMinDirLen == 1e-5f && kHighTarget == 265.0f && near(kSmoothTime, 0.03f, 1e-7f), "literals match libil2cpp .rodata");
        CHECK(predictCueRotationTarget(100, kHighTarget).target == 265.0f, "93..180 band = 265");
        TargetInput i; i.speed = 1; i.x = 0.5e-5f; i.z = 0;
        i.fallback.x = 1;
        CHECK(targetBallVelocity(i).x == 1.0f, "length below 1e-5 uses fallback");
    }
    {   // SmoothDamp converges without overshoot
        float v = 0, x = 0;
        for (int k = 0; k < 600; ++k) x = smoothDamp(x, 100.0f, v, 0.03f, std::numeric_limits<float>::infinity(), 1.0f / 60.0f);
        CHECK(near(x, 100.0f, 1e-2f) && x <= 100.0f, "SmoothDamp converges monotonically");
    }
    std::printf("%d checks, %d failed\n", n, fail);
    return fail ? 1 : 0;
}
