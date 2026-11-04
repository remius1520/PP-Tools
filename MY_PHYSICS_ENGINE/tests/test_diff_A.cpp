// Differential test: two independent ARM64 ports of SpinController.CalcMaxSpinValues / ApplySpinFactorCombination must agree.
#include "../src/pp2_spin.hpp"
#include <cstdio>
#include <random>
namespace A { 
#include "../ref/pp2_spin_A_independent.hpp"
}
int main() {
    std::mt19937 g(5); auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(g); };
    int n = 0, bad = 0; double worst = 0;
    for (int it = 0; it < 20000; ++it) {
        int type = (g() % 3 == 0) ? 2 : 0;
        pp2::spin::MaxSpinValuesInput in; in.radius = U(5, 40); in.currentDifference = U(0, 60); in.zeroDifferenceMultiplication = U(0, 1); in.globalBias = (double)-0.22f;
        in.externalBaseline = U(0.5, 8); double F = U(0.5, 10), H = U(0, 5); in.cueBallRadius = H; in.vectorX = F * 0.6; in.vectorY = 0; in.vectorZ = F * 0.8;
        in.minBaseIncrementValue = U(0, 1); in.maxBaseIncrementValue = U(0, 1); in.minBaseDecreaseRate = U(0, 1); in.maxBaseDecreaseRate = U(0, 1); in.minBaseIncreaseAfterZero = U(0, 1); in.maxBaseIncreaseAfterZero = U(0, 1);
        in.minBaseIncreaseSide = U(0, 1); in.maxBaseIncreaseSide = U(0, 1); in.maxBaseIncreaseEdge = U(0, 1); in.maxBaseDecreaseEdge = U(0, 1);
        in.leftIncrement = U(-1, 1); in.rightIncrement = U(-1, 1); in.bottomIncrement = U(-1, 1); in.spinType = type;
        auto r = pp2::spin::calcMaxSpinValues(in); if (!r.valid) continue;
        A::pp2::spin::State S; S.currentDiff = in.currentDifference; S.zeroDiffMul = in.zeroDifferenceMultiplication; S.leftInc = in.leftIncrement; S.rightInc = in.rightIncrement; S.bottomInc = in.bottomIncrement; S.spinType = type;
        A::pp2::spin::Tune T; T.minInc = in.minBaseIncrementValue; T.maxInc = in.maxBaseIncrementValue; T.minDec = in.minBaseDecreaseRate; T.maxDec = in.maxBaseDecreaseRate; T.minAZ = in.minBaseIncreaseAfterZero; T.maxAZ = in.maxBaseIncreaseAfterZero; T.minSide = in.minBaseIncreaseSide; T.maxSide = in.maxBaseIncreaseSide; T.maxEdgeInc = in.maxBaseIncreaseEdge; T.maxEdgeDec = in.maxBaseDecreaseEdge;
        A::pp2::spin::calcMaxSpinValues(S, T, in.radius, in.externalBaseline, F, H);
        double d[4] = {std::fabs(r.maxUp - S.maxUp), std::fabs(r.maxLeft - S.maxLeft), std::fabs(r.maxRight - S.maxRight), std::fabs(r.maxBottom - S.maxBottom)};
        ++n; double m = *std::max_element(d, d + 4); worst = std::max(worst, m); if (m > 1e-6) { if (bad < 5) printf("DIFF type=%d  up %.5f/%.5f left %.5f/%.5f right %.5f/%.5f bottom %.5f/%.5f\n", type, r.maxUp, S.maxUp, r.maxLeft, S.maxLeft, r.maxRight, S.maxRight, r.maxBottom, S.maxBottom); ++bad; }
    }
    printf("calcMaxSpinValues: %d cases, %d differ, worst %.3g\n", n, bad, worst);
    // ApplySpinFactorCombination + GetSpinAngelMapping with random live-like fields
    int m = 0, bad2 = 0, bad3 = 0; double w2 = 0;
    for (int it = 0; it < 40000; ++it) {
        pp2::spin::ApplyCombinationInput in; in.horizontal = U(-12, 12); in.vertical = U(-12, 12); in.radius = U(5, 40); in.constRadius = U(4, 14); in.spinType = (g() % 3 == 0) ? 2 : 0;
        auto& d = in.directional; d.baseline = U(0.5, 8); d.maxUp = U(0, 12); d.maxLeft = U(0, 12); d.maxRight = U(0, 12); d.maxBottom = U(0, 12); d.leftIncrement = U(-1, 1); d.rightIncrement = U(-1, 1); d.bottomIncrement = U(-1, 1); d.deadZone = U(0, 3); d.deadZoneVertical = U(0, 3);
        d.circle.currentDifference = U(0, 12); d.circle.zeroDifferenceMultiplication = U(0, 1.2); d.circle.constRadius = in.constRadius;
        auto r = pp2::spin::applySpinFactorCombination(in); if (!r.valid) continue;
        A::pp2::spin::State S; S.constRadius = in.constRadius; S.zeroDiffMul = d.circle.zeroDifferenceMultiplication; S.currentDiff = d.circle.currentDifference; S.deadZone = d.deadZone; S.deadZoneV = d.deadZoneVertical; S.leftInc = d.leftIncrement; S.rightInc = d.rightIncrement; S.bottomInc = d.bottomIncrement; S.maxUp = d.maxUp; S.maxLeft = d.maxLeft; S.maxRight = d.maxRight; S.maxBottom = d.maxBottom; S.spinType = in.spinType;
        double ref = A::pp2::spin::applySpinFactorCombination(S, in.horizontal, in.vertical, in.radius, d.baseline); ++m; double e = std::fabs(ref - r.factor); w2 = std::max(w2, e); if (e > 1e-6) { if (bad2 < 4) printf("DIFF apply type=%d got %.5f ref %.5f (h=%.2f v=%.2f)\n", in.spinType, r.factor, ref, in.horizontal, in.vertical); ++bad2; }
        pp2::spin::MappingInput mi; mi.horizontal = in.horizontal / 10; mi.vertical = in.vertical / 10; mi.currentDifference = U(0, 12); mi.intBallAngle = (int)U(0, 180); mi.scaleX = mi.scaleY = U(0.5, 3);
        auto a = pp2::spin::mapTipToAngle(mi); double b = A::pp2::spin::spinAngleDeg(mi.horizontal, mi.vertical, mi.currentDifference, mi.intBallAngle, mi.scaleX);
        if (a.valid && std::fabs(a.angleDeg - b) > 0.11 && std::fabs(std::fabs(a.angleDeg - b) - 360) > 0.11) { if (bad3 < 4) printf("DIFF angle %.2f vs %.2f\n", a.angleDeg, b); ++bad3; }
    }
    printf("applySpinFactorCombination: %d cases, %d differ, worst %.3g | spin angle: %d differ\n", m, bad2, w2, bad3);
    return (bad != 0) || (bad2 != 0) || (bad3 != 0);
}
