// PP2 Part A: SpinController spin-factor maths, ported 1:1 from libil2cpp.so (ARM64): DivideTheCircle 0x5DA0E7C, ApplySpinFactorCombination 0x5D9EF80.
// Every input is a LIVE FIELD of the game (the payload reads them; nothing here is fitted):
//   SpinController: constRadius 0xB4, spinType 0xD8, leftIncreamnt 0xFC, rightIncreamnt 0x100, bottomIncreament 0x104, maxSpinFactorUp/Left/Right/Bottom 0x108/0x10C/0x110/0x114,
//                   zerodifferencemultiplication 0x158, deadZone 0x15C, deadZoneverticall 0x160, currentDifference 0x164
//   f (= "b" in the asm) = GamePhysics.Instance.physicsBuilder.finalVelocity (PhysicsBuilder +0x40)  [GamePhysics.physicsBuilder @0x20]
// NOT ported yet: CalcMaxSpinValues (0x5DA00C4) - it WRITES the max/increment fields above; until it is ported they must be read live.
#pragma once
#include <cmath>
#include <algorithm>
namespace pp2 { namespace spin {

struct State {
    double constRadius = 0, zeroDiffMul = 0, currentDiff = 0, deadZone = 0, deadZoneV = 0;
    double leftInc = 0, rightInc = 0, bottomInc = 0;
    double maxUp = 0, maxLeft = 0, maxRight = 0, maxBottom = 0;
    int spinType = 0;
};
inline double clamp01(double v) { return v > 1.0 ? 1.0 : (v < 0.0 ? 0.0 : v); }

// DivideTheCircle(horizontal h, vertical v, radius r): writes spinFactorx (0x11C) and spinFactory (0x120).
inline void divideTheCircle(const State& S, double h, double v, double r, double f, double& sfx, double& sfy) {
    const double a = std::fabs(h), b = std::fabs(v), C = S.constRadius, Z = S.currentDiff * S.zeroDiffMul;
    double nx = clamp01(a / C), ny = clamp01(b / C), px = 0, py = 0;
    if (Z != C) { px = clamp01((a - Z) / (C - Z)); py = clamp01((b - Z) / (C - Z)); }
    const double w = clamp01((r - Z) / r);
    double mL = S.maxLeft, mR = S.maxRight, mB = S.maxBottom;
    if (w > 0.0) {
        if (S.rightInc < 0.0) {
            nx = h > 0 ? a / Z : nx; ny = clamp01((b < S.deadZone ? 0.0 : b - S.deadZone) / C); mR = mR * std::max(px, 0.0);
        } else if (S.leftInc < 0.0) {
            nx = h < 0 ? a / Z : nx; ny = clamp01((b < S.deadZone ? 0.0 : b - S.deadZone) / C); mL = mL * std::max(px, 0.0);
        } else if (S.bottomInc < 0.0) {
            ny = v < 0 ? b / Z : ny; nx = clamp01((a < S.deadZoneV ? 0.0 : a - S.deadZoneV) / C); mB = mB * std::max(py, 0.0);
        }
    }
    const double hm = h > 0 ? mR : mL, vm = v > 0 ? S.maxUp : mB;
    sfx = f + (hm - f) * clamp01(nx);
    sfy = f + (vm - f) * clamp01(ny);
}

// ApplySpinFactorCombination(h, v, r) -> SpinFactor.  f = finalVelocity.
inline double applySpinFactorCombination(const State& S, double h, double v, double r, double f) {
    const double M = std::max(std::max(S.maxRight, S.maxLeft), std::max(S.maxUp, S.maxBottom));
    const double t = clamp01(std::fabs(v) / (S.constRadius - 3.0));
    double s;
    if (S.spinType == 2) { const double d = v >= 0 ? S.maxUp - f : -S.maxBottom; s = f + t * d; return s < 0 ? 0 : (s > M ? M : s); }
    double fx, fy; divideTheCircle(S, h, v, r, f, fx, fy);
    const double side = h > 0 ? S.maxRight : S.maxLeft, vert = v > 0 ? S.maxUp : S.maxBottom;
    s = fx + fy - f; const double cap = std::max(side, vert);
    return s < 0 ? 0 : (s > cap ? cap : s);
}

}}
