// PP2 Part A: SpinController spin-factor maths, ported 1:1 from libil2cpp.so (ARM64): DivideTheCircle 0x5DA0E7C, ApplySpinFactorCombination 0x5D9EF80.
// Every input is a LIVE FIELD of the game (the payload reads them; nothing here is fitted):
//   SpinController: constRadius 0xB4, spinType 0xD8, leftIncreamnt 0xFC, rightIncreamnt 0x100, bottomIncreament 0x104, maxSpinFactorUp/Left/Right/Bottom 0x108/0x10C/0x110/0x114,
//                   zerodifferencemultiplication 0x158, deadZone 0x15C, deadZoneverticall 0x160, currentDifference 0x164
//   f (= "b" in the asm) = GamePhysics.Instance.physicsBuilder.finalVelocity (PhysicsBuilder +0x40)  [GamePhysics.physicsBuilder @0x20]
// CalcMaxSpinValues (0x5DA00C4) is ported below (it WRITES the four max fields); the live fields can still be read instead.
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


// ---- CalcMaxSpinValues(radius R) [0x5DA00C4]: computes maxSpinFactorUp/Left/Right/Bottom (0x108/0x10C/0x110/0x114) ----
// Tune = the [SerializeField] constants of SpinController: minbaseIncrementValue 0x130, maxbaseIncrementValue 0x134, minbasedecreaseRate 0x138, maxbasedecreaseRate 0x13C,
//        minbaseIncreaseAfterZero 0x140, maxbaseIncreaseAfterZero 0x144, minbaseIncreaseSide 0x148, maxbaseIncreaseSide 0x14C, maxBaseIncreaseEdge 0x150, maxBaseDecreaseEdge 0x154.
// f = PhysicsBuilder.finalVelocity (+0x40), F = |PhysicsBuilder.forceDirection| (+0x28..0x30, 3-D length), H = CueBall.hitDistance (+0x25C).
// leftInc/rightInc/bottomInc (0xFC/0x100/0x104) are INPUTS here (written elsewhere in the class); a negative one switches that side to the "decrease" formulas.
struct Tune { double minInc = 0, maxInc = 0, minDec = 0, maxDec = 0, minAZ = 0, maxAZ = 0, minSide = 0, maxSide = 0, maxEdgeInc = 0, maxEdgeDec = 0; };
inline void calcMaxSpinValues(State& S, const Tune& T, double R, double f, double F, double H) {
    const double Z = S.currentDiff * S.zeroDiffMul;
    const double u = clamp01(S.currentDiff / 90.0), c = clamp01(u + (double)-0.22f), v = clamp01((R - Z) / R);   // -0.22f: float constant in libil2cpp rodata
    const double inc = T.maxInc + (T.minInc - T.maxInc) * u, dec = T.minDec + (T.maxDec - T.minDec) * c, az = T.minAZ + (T.maxAZ - T.minAZ) * v, side = T.maxSide + (T.minSide - T.maxSide) * u;
    const double dF = F - H; const bool t2 = S.spinType == 2;
    const double decay = v > 0 ? az * dF : dec * f;
    S.maxUp = f + (t2 ? T.maxEdgeInc * f : inc * dF);
    S.maxLeft = S.leftInc < 0 ? decay : (t2 ? S.maxUp * 0.5 : f + side * dF);
    S.maxBottom = t2 ? f * T.maxEdgeDec : (S.bottomInc < 0 ? decay : f + inc * dF);
    S.maxRight = S.rightInc < 0 ? decay : (t2 ? S.maxBottom * 0.5 : f + side * dF);
}
// NetworkVariable biggerSpinPointVal (0x170) = max of the four (written when the server flag @+0x39 is set)
inline double biggerSpinPoint(const State& S) { return std::max(std::max(S.maxUp, S.maxLeft), std::max(S.maxRight, S.maxBottom)); }


// ---- GetSpinAngelMapping(h, v) [0x5DA186C]: the spin angle (deg) stored in ballAngelFullRange (0x124) and angleDeg (0xC0) ----
// cd = currentDifference (0x164); D = CueItem.SpinFactorDividor (get_SpinFactorDividor 0x5DF8A88; gameManager+0x180 -> +0x1A0 -> CueItem +0xA0); ballAngleInt = CueBall.intBallAngle (+0x1C4).
//   y = -(k * (ballAngleInt < 90 ? -cd : cd) + h) / D,   x = -(cd * m + v) / D,   with (k, m) = cd < 3 ? (2, 2) : (3, 0.5)
//   theta = atan2(y, x) in degrees, wrapped into (-180, 180], Math.Round(.., 1, ToEven); an exact 0 becomes 0.1 (static float @ rodata).
// The two Mathf thunks are not named in the binary: atan2 is certain from the operands; the second one only matters for the single point theta == +180.
inline double spinAngleDeg(double h, double v, double cd, int ballAngleInt, double D) {
    const double k = cd < 3 ? 2.0 : 3.0, m = cd < 3 ? 2.0 : 0.5, sg = ballAngleInt < 90 ? -cd : cd;
    const double y = -(k * sg + h) / D, x = -(cd * m + v) / D;
    double th = std::atan2(y, x) * (180.0 / 3.14159265358979323846);
    if (!(th > -180.0 && th < 180.0)) th = th >= 180.0 ? th - 360.0 : th;         // keep (-180, 180]; atan2 never returns outside [-180,180]
    th = std::nearbyint(th * 10.0) / 10.0;
    return th == 0.0 ? 0.1 : th;
}

}}
