// P2L - wires the decoded SpinController pipeline (pp2_spin.hpp) to the simulator.
// Sources (all from game files unless marked ASSUMED):
//  * SpinController serialized values: scene level5 (data.unity3d), GameObject "SpinController", MonoBehaviour script 4894
//    (raw layout checked against dump.cs offsets).                                                   [level5]
//  * globalBias = -0.22: float in libil2cpp rodata at VA 0x1318854 (loaded by CalcMaxSpinValues 0x5DA0178).   [libil2cpp]
//  * externalBaseline: GamePhysics.Instance -> physicsBuilder(+0x20) -> +0x40 = PhysicsBuilder.finalVelocity;
//    vector (+0x28/+0x2C/+0x30 of the same object) = PhysicsBuilder.forceDirection;
//    "cueBallRadius" (CueBall+0x25C) is really CueBall.hitDistance.                                   [libil2cpp dump.cs + ARM64 0x5DA01E0..0x5DA0248]
//    (identification of the singleton as GamePhysics: slot 0x74dba18 is the same one used by PhysicsBuilder consumers = inferred, see FINDINGS_P2L)
//  * cue SpinFactorDividor: data/cueitems_effective.csv (getter: hasValue?value:0.125).               [resources.assets]
//  * P2N (ARM64, FINDINGS_P2O): ClientSpinController.Update (0x359A674): |h|<=1 && |v|<=1 -> SpinController.ResetSpinValues(); else
//    GetAngelAndSpinFactorLimitions(h, v, radius) (0x5DA3BD8). h/v = UI units, each clamped to [min*D, max*D] (D = Stick.StickSpinFactorDividor),
//    radius = SpinController.constRadius(+0xB4) * D (SpinAnchorUpdate 0x3599650).  currentDifference (+0x164) = |CueBall.intBallAngle - 90|.
//    spinType (+0xD8): Ball = 1, otherwise 2 (Edge).  Ball: GetSpinAngelMapping; Edge: ApplySpinAngelCombinationForEdge (0x5D9EA40).
//  * ASSUMED: which cue the player uses (default dividor 0.75 = most common); the tip is on the circle's square clamp, not the circle.
#pragma once
#include "pp2_spin.hpp"
#include "pp2_engine.hpp"

namespace pp2 { namespace spinwire {

// SpinController MonoBehaviour in level5 (raw body, matched to dump.cs offsets)
struct SceneCfg {
    double maxX = 60, minX = -60, maxY = 60, minY = -60, controllSpeed = 0.3, constRadius = 60;   // minX/maxX/minY/maxY: SpinAnchorUpdate clamps h/v to [min*D, max*D]
    double leftInc = 1, rightInc = 1, bottomInc = 1;                       // +0xFC,+0x100,+0x104
    double minBaseInc = 0.2, maxBaseInc = 0.4;                             // +0x130,+0x134
    double minBaseDec = 0.1, maxBaseDec = 0.3;                             // +0x138,+0x13C
    double minAfterZero = 0.17, maxAfterZero = 0.37;                       // +0x140,+0x144
    double minSide = 0.25, maxSide = 0.45;                                 // +0x148,+0x14C
    double maxEdgeInc = 0.5, maxEdgeDec = 0.6;                             // +0x150,+0x154
    double zeroDiffMul = 3, deadZone = 7, deadZoneVert = 7;                // +0x158,+0x15C,+0x160
    double globalBias = -0.2199999988079071;                               // libil2cpp rodata 0x1318854
    double currentDifference = 0;                                          // used only when useGivenDifference; default = |intBallAngle - 90| (P2N)
    bool useGivenDifference = false;
};

struct Result { bool valid = false; double spinFactor = 0, spinAngleDeg = 0; spin::SpinPipelineResult raw; };

// forceDirection length F, hitDistance, finalVelocity come from the shot prediction.
inline Result fromTip(const SceneCfg& c, double horizontal, double vertical, double radius, double cueDividor,
                      double F, double hitDistance, double finalVelocity, int intBallAngle, int spinType = 0) {
    spin::SpinPipelineInput in;
    in.horizontal = horizontal; in.vertical = vertical; in.radius = radius; in.constRadius = c.constRadius;
    in.deadZone = c.deadZone; in.deadZoneVertical = c.deadZoneVert; in.spinType = spinType;
    auto& m = in.maxima;
    m.currentDifference = c.currentDifference; m.zeroDifferenceMultiplication = c.zeroDiffMul; m.globalBias = c.globalBias;
    m.externalBaseline = finalVelocity; m.cueBallRadius = hitDistance; m.vectorX = F; m.vectorY = 0; m.vectorZ = 0;
    m.minBaseIncrementValue = c.minBaseInc; m.maxBaseIncrementValue = c.maxBaseInc;
    m.minBaseDecreaseRate = c.minBaseDec; m.maxBaseDecreaseRate = c.maxBaseDec;
    m.minBaseIncreaseAfterZero = c.minAfterZero; m.maxBaseIncreaseAfterZero = c.maxAfterZero;
    m.minBaseIncreaseSide = c.minSide; m.maxBaseIncreaseSide = c.maxSide;
    m.maxBaseIncreaseEdge = c.maxEdgeInc; m.maxBaseDecreaseEdge = c.maxEdgeDec;
    m.leftIncrement = c.leftInc; m.rightIncrement = c.rightInc; m.bottomIncrement = c.bottomInc;
    in.mapping.cueSpinFactorDividor = cueDividor; in.mapping.intBallAngle = intBallAngle;
    in.mapping.currentDifference = c.currentDifference;
    Result r; r.raw = spin::evaluateSpinPipeline(in); r.valid = r.raw.valid;
    r.spinFactor = r.raw.spinFactor; r.spinAngleDeg = r.raw.spinAngleDeg; return r;
}

// ApplySpinAngelCombinationForEdge 0x5D9EA40 (ARM64): angleDeg for Edge hits.  ratio = clamp(|h|/radius, 0, 1); a = intBallAngle > 90 ? 180 - intBallAngle : intBallAngle;
// result = ratio * 0.5 * (h < 0 ? +a : -a).
inline double edgeAngle(double h, double radius, int intBallAngle) {
    double ratio = std::fabs(h) / radius; ratio = ratio > 1.0 ? 1.0 : ratio; if (ratio < 0.0) ratio = 0.0;
    int a = intBallAngle > 90 ? 180 - intBallAngle : intBallAngle;
    return ratio * 0.5 * (h < 0 ? (double)a : -(double)a);
}

// Tip -> Shot: predict without spin, evaluate SpinController, set Shot.spin / spinAngleDeg.
struct Tip { double h = 0, v = 0, radius = -1 /* <0: constRadius * cueDividor (SpinAnchorUpdate) */, cueDividor = 0.75; int spinType = -1 /* <0: Ball=1 / Edge=2 from the prediction */; };
inline bool applyTip(const Sim& S, const std::vector<Ball>& B, Shot& s, const Tip& t, const SceneCfg& c0 = SceneCfg()) {
    Shot s0 = s; s0.spin = -1; s0.spinAngleDeg = 0;
    Prediction p = S.predict(B, s0); if (!p.valid) return false;
    const double D = t.cueDividor;
    // ClientSpinController.Update: tip inside +-1 on both axes -> ResetSpinValues (no spin; SpinFactor = finalVelocity as without spin)
    if (std::fabs(t.h) <= 1.0 && std::fabs(t.v) <= 1.0) { s.spin = -1; s.spinAngleDeg = 0; s.spinApplied = false; return true; }
    // SpinAnchorUpdate: square clamp to [min*D, max*D]; radius = constRadius * D
    double h = std::min(std::max(t.h, c0.minX * D), c0.maxX * D), v = std::min(std::max(t.v, c0.minY * D), c0.maxY * D);
    double radius = t.radius > 0 ? t.radius : c0.constRadius * D;
    int ang = p.angleInt;                                                  // CueBall.intBallAngle (+0x1C4): computed for Ball and Edge hits
    SceneCfg c = c0; if (!c.useGivenDifference) c.currentDifference = std::fabs((double)ang - 90.0);
    int type = t.spinType >= 0 ? t.spinType : (p.isBall ? 1 : 2);
    Result r = fromTip(c, h, v, radius, D, s.F, p.hitDistance, p.finalVelocity, ang, type);
    if (!r.valid) return false;
    double angle = type == 1 ? r.spinAngleDeg : edgeAngle(h, radius, ang);
    s.spin = r.spinFactor; s.spinAngleDeg = angle; s.spinApplied = true; return true;
}
}} // namespace
