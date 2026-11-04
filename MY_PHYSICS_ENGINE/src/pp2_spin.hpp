#pragma once
#include <cmath>
#include <algorithm>

// SpinController.GetSpinAngelMapping (RVA 0x5DA186C), reconstructed as a
// standalone pure function. The two scale values are the results of the two
// CueItem.SpinFactorDividor supplies both divisors in the observed call path
// (getter RVA 0x5DF8A88; serialized Nullable<float> backing field +0x50).
namespace pp2 { namespace spin {
struct MappingInput {
    double horizontal = 0.0;       // ClientSpinController.horizontal
    double vertical = 0.0;         // ClientSpinController.vertical
    double currentDifference = 0.0;// SpinController.currentDifference (+0x164)
    int intBallAngle = 90;          // CueBall angle used for the sign branch
    double scaleX = 1.0;            // first transform-derived divisor (s10)
    double scaleY = 1.0;            // second transform-derived divisor (s11)
    double fallbackAngleDeg = 0.10000000149011612;  // rodata float 0.1 used when the rounded angle is exactly 0 (NOT a field)
};
struct MappingResult {
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
    double angleDeg = 0.0;
    double kHorizontal = 0.0;
    double kVertical = 0.0;
};
inline double wrap180(double deg) {
    double v = std::fmod(deg + 180.0, 360.0);
    if (v < 0.0) v += 360.0;
    return v - 180.0;
}
inline MappingResult mapTipToAngle(const MappingInput& in) {
    MappingResult out;
    if (!std::isfinite(in.horizontal) || !std::isfinite(in.vertical) ||
        !std::isfinite(in.currentDifference) || !std::isfinite(in.scaleX) ||
        !std::isfinite(in.scaleY) || !std::isfinite(in.fallbackAngleDeg) ||
        std::fabs(in.scaleX) < 1e-9 || std::fabs(in.scaleY) < 1e-9) return out;

    // ARM64 evidence:
    // currentDifference < 3 => horizontal multiplier 2, else 3.
    // currentDifference < 3 => vertical multiplier 2, else 0.5.
    // The horizontal currentDifference term changes sign when IntBallAngle < 90.
    out.kHorizontal = in.currentDifference < 3.0 ? 2.0 : 3.0;
    out.kVertical = in.currentDifference < 3.0 ? 2.0 : 0.5;
    const double signedDifference = in.intBallAngle < 90
        ? -in.currentDifference : in.currentDifference;
    // FIX (P2F, ARM64 0x5DA1980..0x5DA199C): Atan2's FIRST argument (s0) is the HORIZONTAL term -(kH*signed+h)/D2, the SECOND (s1) the VERTICAL term -(kV*cd+v)/D1.
    // The previous port had them swapped, which turns the angle into 90deg - angle. Divisors: s11 (2nd getter call) for the horizontal term, s10 (1st call) for the vertical one.
    // Field naming kept from the original port: x = horizontal term (/scaleY), y = vertical term (/scaleX).
    out.x = -(out.kHorizontal * signedDifference + in.horizontal) / in.scaleY;
    out.y = -(out.kVertical * in.currentDifference + in.vertical) / in.scaleX;

    double angle = wrap180(std::atan2(out.x, out.y) * 57.29577951308232);   // atan2(first = horizontal term, second = vertical term)
    // Original rounds to one decimal (Math.Round ToEven) and substitutes the rodata float 0.1 (0x1319840) when the result is exactly 0.
    // FIX (P2F): the substitute is that constant, NOT the ballAngelFullRange field. fallbackAngleDeg is kept as an override defaulting to 0.1.
    angle = std::nearbyint(angle * 10.0) / 10.0;
    if (angle == 0.0) angle = in.fallbackAngleDeg;
    out.angleDeg = angle;
    out.valid = true;
    return out;
}

// Bound-source adapter for GetSpinAngelMapping. The ARM64 method calls
// CueItem.get_SpinFactorDividor (RVA 0x5DF8A88) twice through the same
// GameManager -> current-cue-item dereference path, then uses those returned
// values as the two divisors. The dump identifies CueItem's serialized backing
// field as Nullable<float> spinfactorDividor at +0x50. The effective getter
// value is supplied here; Nullable fallback/default semantics are not inferred.
struct CueMappingInput {
    double horizontal = 0.0;
    double vertical = 0.0;
    double currentDifference = 0.0;
    int intBallAngle = 90;
    double cueSpinFactorDividor = 1.0; // CueItem.SpinFactorDividor getter
    double fallbackAngleDeg = 0.0;
};
inline MappingResult mapTipToAngleWithCueItem(const CueMappingInput& in) {
    MappingInput mapped;
    mapped.horizontal = in.horizontal;
    mapped.vertical = in.vertical;
    mapped.currentDifference = in.currentDifference;
    mapped.intBallAngle = in.intBallAngle;
    mapped.scaleX = in.cueSpinFactorDividor;
    mapped.scaleY = in.cueSpinFactorDividor;
    mapped.fallbackAngleDeg = in.fallbackAngleDeg;
    return mapTipToAngle(mapped);
}

// Geometry pre-pass of SpinController.CalcMaxSpinValues (RVA 0x5DA00C4).
// ARM64 evidence: it reads the cue-ball radius at CueBall+0x25C, reads a
// three-component vector from a referenced transform/object (+0x28/+0x2C/+0x30),
// computes sqrt(x*x+y*y+z*z)-cueBallRadius, and independently forms two clamped
// weights: currentDifference/90 and (radius-currentDifference*zeroMultiplier)/radius.
// This is an intermediate geometry envelope only; class-limiters and the four
// final maxima are not inferred here.
struct MaxSpinGeometryInput {
    double vectorX = 0.0;
    double vectorY = 0.0;
    double vectorZ = 0.0;
    double cueBallRadius = 0.0; // CueBall +0x25C
    double currentDifference = 0.0; // SpinController +0x164
    double zeroDifferenceMultiplication = 0.0; // +0x158
    double radius = 1.0; // CalcMaxSpinValues argument
};
struct MaxSpinGeometryResult {
    bool valid = false;
    double vectorLength = 0.0;
    double surfaceDistance = 0.0;
    double differenceWeight = 0.0;
    double radiusHeadroomWeight = 0.0;
};
inline MaxSpinGeometryResult calcMaxSpinGeometry(const MaxSpinGeometryInput& in) {
    MaxSpinGeometryResult out;
    const double v[] = {in.vectorX,in.vectorY,in.vectorZ,in.cueBallRadius,
        in.currentDifference,in.zeroDifferenceMultiplication,in.radius};
    for (double x : v) if (!std::isfinite(x)) return out;
    if (in.cueBallRadius < 0.0 || in.radius <= 0.0) return out;
    out.vectorLength = std::sqrt(in.vectorX*in.vectorX + in.vectorY*in.vectorY + in.vectorZ*in.vectorZ);
    out.surfaceDistance = out.vectorLength - in.cueBallRadius;
    out.differenceWeight = std::max(0.0, std::min(1.0, in.currentDifference / 90.0));
    out.radiusHeadroomWeight = std::max(0.0, std::min(1.0, (in.radius - in.currentDifference*in.zeroDifferenceMultiplication) / in.radius));
    out.valid = std::isfinite(out.surfaceDistance);
    return out;
}

// First normalization stage of SpinController.DivideTheCircle (RVA 0x5DA0E7C).
// ARM64 evidence computes abs(horizontal)/constRadius and abs(vertical)/constRadius,
// clamps each to [0,1], then computes the residual coordinates relative to
// currentDifference*zeroDifferenceMultiplication when that offset differs from
// constRadius. Later directional interpolation is deliberately kept separate
// until its dead-zone and increment inputs are fully bound.
struct CircleNormalizationInput {
    double horizontal = 0.0;
    double vertical = 0.0;
    double constRadius = 1.0; // SpinController +0xB4
    double currentDifference = 0.0; // +0x164
    double zeroDifferenceMultiplication = 0.0; // +0x158
    double radius = 1.0; // third DivideTheCircle argument (ARM64 s10)
};
struct CircleNormalizationResult {
    bool valid = false;
    double absHorizontal = 0.0;
    double absVertical = 0.0;
    double normalizedHorizontal = 0.0;
    double normalizedVertical = 0.0;
    double spinOffset = 0.0;
    double residualHorizontal = 0.0;
    double residualVertical = 0.0;
    double directionalRadiusWeight = 0.0; // clamp01((radius-spinOffset)/radius)
    bool residualDefined = false;
};
inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

// CalcMaxSpinValues coefficient decomposition (RVA 0x5DA00C4).
// This exposes only arithmetic directly visible in ARM64 before the final
// directional maxima are written. Inspector fields are caller-supplied because
// their owning serialized configuration/class-limiter objects are not yet bound.
// ARM64 computes these deltas and scales them by the corresponding clamped
// weights; globalBias is the value loaded from the global data slot at image+0x854.
struct MaxSpinCoefficientInput {
    double currentDifference = 0.0; // SpinController +0x164
    double zeroDifferenceMultiplication = 0.0; // +0x158
    double radius = 1.0; // CalcMaxSpinValues argument
    double globalBias = 0.0; // global data slot referenced at image+0x854
    double cueBallRadius = 0.0; // CueBall +0x25C
    double vectorX = 0.0, vectorY = 0.0, vectorZ = 0.0; // referenced transform vector
    double field130 = 0.0, field134 = 0.0;
    double field138 = 0.0, field13C = 0.0;
    double field140 = 0.0, field144 = 0.0;
    double field14C = 0.0, field148 = 0.0;
};
struct MaxSpinCoefficientResult {
    bool valid = false;
    double vectorLength = 0.0;
    double surfaceDistance = 0.0;
    double differenceWeight = 0.0;
    double radiusHeadroomWeight = 0.0;
    double biasedDifferenceWeight = 0.0;
    double delta130_134 = 0.0, term130_134 = 0.0;
    double delta138_13C = 0.0, term138_13C = 0.0;
    double delta140_144 = 0.0, term140_144 = 0.0;
    double delta14C_148 = 0.0, term14C_148 = 0.0;
};
inline MaxSpinCoefficientResult calcMaxSpinCoefficientTerms(const MaxSpinCoefficientInput& in) {
    MaxSpinCoefficientResult out;
    const double values[] = {in.currentDifference,in.zeroDifferenceMultiplication,in.radius,
        in.globalBias,in.cueBallRadius,in.vectorX,in.vectorY,in.vectorZ,
        in.field130,in.field134,in.field138,in.field13C,in.field140,in.field144,
        in.field14C,in.field148};
    for (double v : values) if (!std::isfinite(v)) return out;
    if (in.radius <= 0.0 || in.cueBallRadius < 0.0) return out;
    out.vectorLength = std::sqrt(in.vectorX*in.vectorX + in.vectorY*in.vectorY + in.vectorZ*in.vectorZ);
    out.surfaceDistance = out.vectorLength - in.cueBallRadius;
    out.differenceWeight = clamp01(in.currentDifference / 90.0);
    out.radiusHeadroomWeight = clamp01((in.radius - in.currentDifference*in.zeroDifferenceMultiplication) / in.radius);
    out.biasedDifferenceWeight = clamp01(out.differenceWeight + in.globalBias);
    out.delta130_134 = in.field130 - in.field134;
    out.term130_134 = out.delta130_134 * out.differenceWeight * out.surfaceDistance;
    out.delta138_13C = in.field13C - in.field138;
    out.term138_13C = out.delta138_13C * out.biasedDifferenceWeight;
    out.delta140_144 = in.field144 - in.field140;
    out.term140_144 = out.delta140_144 * out.radiusHeadroomWeight;
    out.delta14C_148 = in.field148 - in.field14C;
    out.term14C_148 = out.delta14C_148 * out.differenceWeight;
    out.valid = std::isfinite(out.term130_134) && std::isfinite(out.term138_13C) &&
        std::isfinite(out.term140_144) && std::isfinite(out.term14C_148);
    return out;
}

inline CircleNormalizationResult normalizeCircleInput(const CircleNormalizationInput& in) {
    CircleNormalizationResult out;
    if (!std::isfinite(in.horizontal) || !std::isfinite(in.vertical) ||
        !std::isfinite(in.constRadius) || !std::isfinite(in.currentDifference) ||
        !std::isfinite(in.zeroDifferenceMultiplication) || !std::isfinite(in.radius) ||
        in.constRadius <= 0.0 || in.radius <= 0.0) return out;
    out.absHorizontal = std::fabs(in.horizontal);
    out.absVertical = std::fabs(in.vertical);
    out.normalizedHorizontal = clamp01(out.absHorizontal / in.constRadius);
    out.normalizedVertical = clamp01(out.absVertical / in.constRadius);
    out.spinOffset = in.currentDifference * in.zeroDifferenceMultiplication;
    // ARM64 at RVA 0x5DA0F64..0x5DA0F84 computes the third-argument
    // radius headroom independently of constRadius and clamps it to [0,1].
    out.directionalRadiusWeight = clamp01((in.radius - out.spinOffset) / in.radius);
    if (out.spinOffset != in.constRadius) {
        const double denominator = in.constRadius - out.spinOffset;
        if (std::fabs(denominator) < 1e-12) return CircleNormalizationResult{};
        out.residualHorizontal = clamp01((out.absHorizontal - out.spinOffset) / denominator);
        out.residualVertical = clamp01((out.absVertical - out.spinOffset) / denominator);
        out.residualDefined = true;
    }
    out.valid = true;
    return out;
}

// SpinController.SetMaxSpinBiggerPoint (RVA 0x5D9EC9C).
// The value loaded from the external level/config object at +0x48 is supplied
// by the caller because that object's semantic identity is not yet proven.
// spinType == 2 selects maxBaseIncreaseEdge (+0x150); all other values select
// max(minBaseDecreaseRate (+0x138), minBaseIncreaseSide (+0x148)).
struct BiggerPointInput {
    double sourceBase = 0.0;          // external object field +0x48
    double hitDistance = 0.0;         // method argument
    int spinType = 0;
    double maxBaseIncreaseEdge = 0.0; // SpinController +0x150
    double minBaseDecreaseRate = 0.0; // SpinController +0x138
    double minBaseIncreaseSide = 0.0; // SpinController +0x148
};
struct BiggerPointResult {
    bool valid = false;
    double selectedFactor = 0.0;
    double adjustedBase = 0.0;
    double maxSpinBiggerPoint = 0.0;
};
inline BiggerPointResult calcMaxSpinBiggerPoint(const BiggerPointInput& in) {
    BiggerPointResult out;
    if (!std::isfinite(in.sourceBase) || !std::isfinite(in.hitDistance) ||
        !std::isfinite(in.maxBaseIncreaseEdge) ||
        !std::isfinite(in.minBaseDecreaseRate) ||
        !std::isfinite(in.minBaseIncreaseSide)) return out;
    out.selectedFactor = in.spinType == 2
        ? in.maxBaseIncreaseEdge
        : std::max(in.minBaseDecreaseRate, in.minBaseIncreaseSide);
    out.adjustedBase = in.sourceBase - in.hitDistance;
    out.maxSpinBiggerPoint = out.adjustedBase + out.adjustedBase * out.selectedFactor;
    out.valid = true;
    return out;
}

// Full directional-maximum reconstruction of SpinController.CalcMaxSpinValues
// (RVA 0x5DA00C4). Inputs correspond to the serialized fields read by ARM64.
// externalBaseline is the float read from the resolved singleton/config object
// at +0x40; its owning type is not named by the available dump, so it remains
// an explicit input rather than a guessed default.
struct MaxSpinValuesInput {
    double radius = 1.0;                  // method argument
    double currentDifference = 0.0;       // SpinController +0x164
    double zeroDifferenceMultiplication = 0.0; // +0x158
    double globalBias = 0.0;              // global data slot image+0x854
    double externalBaseline = 0.0;        // resolved singleton/config object +0x40
    double cueBallRadius = 0.0;            // CueBall +0x25C
    double vectorX = 0.0, vectorY = 0.0, vectorZ = 0.0; // resolved object +0x28/+2C/+30
    double minBaseIncrementValue = 0.0;   // SpinController +0x130
    double maxBaseIncrementValue = 0.0;   // +0x134
    double minBaseDecreaseRate = 0.0;     // +0x138
    double maxBaseDecreaseRate = 0.0;     // +0x13C
    double minBaseIncreaseAfterZero = 0.0;// +0x140
    double maxBaseIncreaseAfterZero = 0.0;// +0x144
    double minBaseIncreaseSide = 0.0;     // +0x148
    double maxBaseIncreaseSide = 0.0;     // +0x14C
    double maxBaseIncreaseEdge = 0.0;     // +0x150
    double maxBaseDecreaseEdge = 0.0;     // +0x154
    double leftIncrement = 0.0;           // +0x0FC
    double rightIncrement = 0.0;          // +0x100
    double bottomIncrement = 0.0;         // +0x104
    int spinType = 0;                     // +0xD8; value 2 is Edge
};
struct MaxSpinValuesResult {
    bool valid = false;
    double vectorLength = 0.0;
    double surfaceDistance = 0.0;
    double differenceWeight = 0.0;
    double biasedDifferenceWeight = 0.0;
    double radiusHeadroomWeight = 0.0;
    double afterZeroCandidate = 0.0;
    double decreaseCandidate = 0.0;
    double sideCandidate = 0.0;
    double incrementCandidate = 0.0;
    double maxUp = 0.0;                   // +0x108
    double maxLeft = 0.0;                 // +0x10C
    double maxRight = 0.0;                // +0x110
    double maxBottom = 0.0;               // +0x114
    double maxAll = 0.0;                  // max of the four directional values
};
inline MaxSpinValuesResult calcMaxSpinValues(const MaxSpinValuesInput& in) {
    MaxSpinValuesResult out;
    const double values[] = {
        in.radius,in.currentDifference,in.zeroDifferenceMultiplication,in.globalBias,
        in.externalBaseline,in.cueBallRadius,in.vectorX,in.vectorY,in.vectorZ,
        in.minBaseIncrementValue,in.maxBaseIncrementValue,in.minBaseDecreaseRate,
        in.maxBaseDecreaseRate,in.minBaseIncreaseAfterZero,in.maxBaseIncreaseAfterZero,
        in.minBaseIncreaseSide,in.maxBaseIncreaseSide,in.maxBaseIncreaseEdge,
        in.maxBaseDecreaseEdge,in.leftIncrement,in.rightIncrement,in.bottomIncrement
    };
    for (double v : values) if (!std::isfinite(v)) return out;
    if (in.radius <= 0.0 || in.cueBallRadius < 0.0) return out;

    const double B = in.externalBaseline;
    const double D = clamp01(in.currentDifference / 90.0);
    const double H = clamp01((in.radius - in.currentDifference * in.zeroDifferenceMultiplication) / in.radius);
    const double Q = clamp01(D + in.globalBias);
    const double length = std::sqrt(in.vectorX*in.vectorX + in.vectorY*in.vectorY + in.vectorZ*in.vectorZ);
    const double surface = length - in.cueBallRadius;

    // ARM64 register/dataflow: four interpolated configuration candidates.
    const double increment = in.maxBaseIncrementValue +
        (in.minBaseIncrementValue - in.maxBaseIncrementValue) * D;
    // FIX (P2F, verified in ARM64 0x5DA0260..0x5DA0338): slot [sp+0x14] is overwritten with minBaseDecreaseRate (+0x138) before it is read back, so the base is minBaseDecreaseRate, NOT minBaseIncrementValue.
    const double decrease = in.minBaseDecreaseRate +
        (in.maxBaseDecreaseRate - in.minBaseDecreaseRate) * Q;
    const double afterZero = in.minBaseIncreaseAfterZero +
        (in.maxBaseIncreaseAfterZero - in.minBaseIncreaseAfterZero) * H;
    const double side = in.maxBaseIncreaseSide +
        (in.minBaseIncreaseSide - in.maxBaseIncreaseSide) * D;

    const double fallback = decrease * B;
    const double afterZeroScaled = afterZero * surface;
    const double reducedByHeadroom = H > 0.0 ? afterZeroScaled : fallback;

    out.vectorLength = length;
    out.surfaceDistance = surface;
    out.differenceWeight = D;
    out.biasedDifferenceWeight = Q;
    out.radiusHeadroomWeight = H;
    out.incrementCandidate = increment;
    out.decreaseCandidate = decrease;
    out.afterZeroCandidate = afterZero;
    out.sideCandidate = side;

    // +0x108: B + edge increase, or B + increment envelope * surface distance.
    out.maxUp = B + (in.spinType == 2
        ? in.maxBaseIncreaseEdge * B
        : increment * surface);

    // +0x10C: negative left increment selects the shared headroom/decrease
    // envelope; otherwise Edge halves Up and normal mode uses the side envelope.
    out.maxLeft = in.leftIncrement < 0.0
        ? reducedByHeadroom
        : (in.spinType == 2 ? out.maxUp * 0.5 : B + side * surface);

    // +0x114: non-negative bottom increment uses the edge-decrease base;
    // negative increment selects the shared headroom/decrease envelope.
    // FIX (P2F, ARM64 0x5DA03B8..0x5DA0434): Edge (spinType 2) stores B*maxBaseDecreaseEdge WITHOUT looking at bottomIncrement; other types: bottomIncrement<0 -> shared envelope, else B + increment*surface.
    out.maxBottom = in.spinType == 2
        ? B * in.maxBaseDecreaseEdge
        : (in.bottomIncrement < 0.0 ? reducedByHeadroom : B + increment * surface);

    // +0x110: negative right increment selects the shared envelope; otherwise
    // Edge halves Bottom and normal mode adds the edge-decrease surface term.
    out.maxRight = in.rightIncrement < 0.0
        ? reducedByHeadroom
        : (in.spinType == 2 ? out.maxBottom * 0.5 : B + side * surface);   // FIX (P2F, 0x5DA049C): normal mode uses the SIDE envelope (s3 = side), not maxBaseDecreaseEdge

    out.maxAll = std::max(std::max(out.maxUp,out.maxLeft),std::max(out.maxRight,out.maxBottom));
    out.valid = std::isfinite(out.maxUp) && std::isfinite(out.maxLeft) &&
        std::isfinite(out.maxRight) && std::isfinite(out.maxBottom) && std::isfinite(out.maxAll);
    return out;
}

// Directional interpolation stage of SpinController.DivideTheCircle
// (RVA 0x5DA0E7C, ARM64 0x5DA0F90..0x5DA10F8).
// The external baseline loaded from the object at +0x40 is caller-supplied;
// its owning class/semantic name is not established by this dump.
struct CircleDirectionalInput {
    CircleNormalizationInput circle;
    double baseline = 0.0;          // external object +0x40
    double maxUp = 0.0;             // SpinController +0x108
    double maxLeft = 0.0;           // +0x10C
    double maxRight = 0.0;          // +0x110
    double maxBottom = 0.0;         // +0x114
    double leftIncrement = 0.0;     // +0x0FC
    double rightIncrement = 0.0;    // +0x100
    double bottomIncrement = 0.0;   // +0x104
    double deadZone = 0.0;          // +0x15C
    double deadZoneVertical = 0.0;  // +0x160
};
struct CircleDirectionalResult {
    bool valid = false;
    double horizontalWeight = 0.0;
    double verticalWeight = 0.0;
    double selectedHorizontalMaximum = 0.0;
    double selectedVerticalMaximum = 0.0;
    double horizontalValue = 0.0;   // stored at SpinController +0x11C
    double verticalValue = 0.0;     // stored at +0x120
    bool leftBranchScaled = false;
    bool rightBranchScaled = false;
    bool bottomBranchScaled = false;
};
inline CircleDirectionalResult divideCircleDirectional(const CircleDirectionalInput& in) {
    CircleDirectionalResult out;
    const auto& c = in.circle;
    const double vals[] = {in.baseline,in.maxUp,in.maxLeft,in.maxRight,in.maxBottom,
        in.leftIncrement,in.rightIncrement,in.bottomIncrement,in.deadZone,in.deadZoneVertical};
    for (double v : vals) if (!std::isfinite(v)) return out;
    const auto n = normalizeCircleInput(c);
    if (!n.valid) return out;

    double hWeight = n.normalizedHorizontal;
    double vWeight = n.normalizedVertical;
    double leftMax = in.maxLeft;
    double rightMax = in.maxRight;
    double bottomMax = in.maxBottom;

    // The ARM64 branch order is right increment, then left increment, then
    // bottom increment. It is entered only when radius headroom is positive.
    if (n.directionalRadiusWeight > 0.0) {
        if (in.rightIncrement < 0.0) {
            if (c.horizontal > 0.0) {
                if (std::fabs(n.spinOffset) < 1e-12) return CircleDirectionalResult{};
                hWeight = n.absHorizontal / n.spinOffset;
            }
            vWeight = clamp01((n.absVertical - in.deadZone) / c.constRadius);
            rightMax *= n.residualHorizontal;
            out.rightBranchScaled = true;
        } else if (in.leftIncrement < 0.0) {
            if (c.horizontal < 0.0) {
                if (std::fabs(n.spinOffset) < 1e-12) return CircleDirectionalResult{};
                hWeight = n.absHorizontal / n.spinOffset;
            }
            vWeight = clamp01((n.absVertical - in.deadZone) / c.constRadius);
            leftMax *= n.residualHorizontal;
            out.leftBranchScaled = true;
        } else if (in.bottomIncrement < 0.0) {
            if (c.vertical < 0.0) {
                if (std::fabs(n.spinOffset) < 1e-12) return CircleDirectionalResult{};
                vWeight = n.absVertical / n.spinOffset;
            }
            hWeight = clamp01((n.absHorizontal - in.deadZoneVertical) / c.constRadius);
            bottomMax *= n.residualVertical;
            out.bottomBranchScaled = true;
        }
    }

    hWeight = clamp01(hWeight);
    vWeight = clamp01(vWeight);
    out.selectedHorizontalMaximum = c.horizontal > 0.0 ? rightMax : leftMax;
    out.selectedVerticalMaximum = c.vertical > 0.0 ? in.maxUp : bottomMax;
    out.horizontalWeight = hWeight;
    out.verticalWeight = vWeight;
    out.horizontalValue = in.baseline + (out.selectedHorizontalMaximum - in.baseline) * hWeight;
    out.verticalValue = in.baseline + (out.selectedVerticalMaximum - in.baseline) * vWeight;
    out.valid = std::isfinite(out.horizontalValue) && std::isfinite(out.verticalValue);
    return out;
}

// SpinController.ApplySpinFactorCombination (RVA 0x5D9EF80).
// This reconstructs the final combination stage. The baseline is the value
// read from the external config object (+0x40); its owner/name remains unknown.
// The four directional maxima and DivideTheCircle increments are supplied by
// the caller until CalcMaxSpinValues is fully reconstructed.
struct ApplyCombinationInput {
    CircleDirectionalInput directional;
    double horizontal = 0.0;       // method argument
    double vertical = 0.0;         // method argument
    double radius = 1.0;            // method argument
    double constRadius = 1.0;       // SpinController +0xB4
    int spinType = 0;               // SpinController +0xD8
};
struct ApplyCombinationResult {
    bool valid = false;
    double factor = 0.0;
    double maxDirectional = 0.0;
    double normalizedVertical = 0.0;
    bool edgeBranch = false;
    CircleDirectionalResult directionalResult;
};
inline ApplyCombinationResult applySpinFactorCombination(const ApplyCombinationInput& in) {
    ApplyCombinationResult out;
    const double values[] = {in.horizontal,in.vertical,in.radius,in.constRadius,
        in.directional.baseline,in.directional.maxUp,in.directional.maxLeft,
        in.directional.maxRight,in.directional.maxBottom};
    for (double v : values) if (!std::isfinite(v)) return out;
    if (in.radius <= 0.0 || in.constRadius <= 3.0) return out;

    const double B = in.directional.baseline;
    out.maxDirectional = std::max(std::max(in.directional.maxUp,in.directional.maxBottom),
        std::max(in.directional.maxLeft,in.directional.maxRight));

    if (in.spinType == 2) {
        // ARM64 0x5D9F0A8..0x5D9F148: t=clamp(abs(vertical)/(constRadius-3)).
        // Negative vertical uses -maxBottom; positive vertical interpolates B->maxUp.
        const double t = clamp01(std::fabs(in.vertical) / (in.constRadius - 3.0));
        const double raw = in.vertical < 0.0
            ? B - t * in.directional.maxBottom
            : B + t * (in.directional.maxUp - B);
        const double upperClamped = raw > out.maxDirectional ? out.maxDirectional : raw;
        out.factor = raw < 0.0 ? 0.0 : upperClamped;
        out.edgeBranch = true;
        out.normalizedVertical = t;
    } else {
        auto directional = in.directional;
        directional.circle.horizontal = in.horizontal;
        directional.circle.vertical = in.vertical;
        directional.circle.radius = in.radius;
        out.directionalResult = divideCircleDirectional(directional);
        if (!out.directionalResult.valid) return out;
        const double selectedHorizontal = in.horizontal > 0.0
            ? in.directional.maxRight : in.directional.maxLeft;
        const double selectedVertical = in.vertical > 0.0
            ? in.directional.maxUp : in.directional.maxBottom;
        const double selectedMaximum = std::max(selectedHorizontal, selectedVertical);
        const double combined = out.directionalResult.horizontalValue +
            out.directionalResult.verticalValue - B;
        const double upperClamped = combined > selectedMaximum ? selectedMaximum : combined;
        out.factor = combined < 0.0 ? 0.0 : upperClamped;
    }
    out.valid = std::isfinite(out.factor);
    return out;
}


// End-to-end offline SpinController evaluation: the same tip coordinates feed
// the angle mapping and the SpinFactor path. The external singleton baseline,
// cue-item effective divisor, and resolved transform vector remain explicit
// inputs because their runtime sources/default semantics are not encoded in
// the current C++ simulator or fully identified by the dump.
struct SpinPipelineInput {
    CueMappingInput mapping;
    MaxSpinValuesInput maxima;
    double horizontal = 0.0;
    double vertical = 0.0;
    double radius = 1.0;
    double constRadius = 1.0;
    double deadZone = 0.0;
    double deadZoneVertical = 0.0;
    int spinType = 0;
};
struct SpinPipelineResult {
    bool valid = false;
    double spinAngleDeg = 0.0;
    double spinFactor = 0.0;
    MaxSpinValuesResult maxima;
    ApplyCombinationResult combination;
    MappingResult mapping;
};
inline SpinPipelineResult evaluateSpinPipeline(const SpinPipelineInput& in) {
    SpinPipelineResult out;
    if (!std::isfinite(in.horizontal) || !std::isfinite(in.vertical) ||
        !std::isfinite(in.radius) || !std::isfinite(in.constRadius) ||
        !std::isfinite(in.deadZone) || !std::isfinite(in.deadZoneVertical) ||
        in.radius <= 0.0 || in.constRadius <= 3.0) return out;

    auto maxInput = in.maxima;
    maxInput.radius = in.radius;
    maxInput.spinType = in.spinType;
    out.maxima = calcMaxSpinValues(maxInput);
    if (!out.maxima.valid) return out;

    CircleDirectionalInput directional;
    directional.circle.horizontal = in.horizontal;
    directional.circle.vertical = in.vertical;
    directional.circle.constRadius = in.constRadius;
    directional.circle.currentDifference = maxInput.currentDifference;
    directional.circle.zeroDifferenceMultiplication = maxInput.zeroDifferenceMultiplication;
    directional.circle.radius = in.radius;
    directional.baseline = maxInput.externalBaseline;
    directional.maxUp = out.maxima.maxUp;
    directional.maxLeft = out.maxima.maxLeft;
    directional.maxRight = out.maxima.maxRight;
    directional.maxBottom = out.maxima.maxBottom;
    directional.leftIncrement = maxInput.leftIncrement;
    directional.rightIncrement = maxInput.rightIncrement;
    directional.bottomIncrement = maxInput.bottomIncrement;
    directional.deadZone = in.deadZone;
    directional.deadZoneVertical = in.deadZoneVertical;

    ApplyCombinationInput combination;
    combination.directional = directional;
    combination.horizontal = in.horizontal;
    combination.vertical = in.vertical;
    combination.radius = in.radius;
    combination.constRadius = in.constRadius;
    combination.spinType = in.spinType;
    out.combination = applySpinFactorCombination(combination);
    if (!out.combination.valid) return out;

    auto mapInput = in.mapping;
    mapInput.horizontal = in.horizontal;
    mapInput.vertical = in.vertical;
    mapInput.currentDifference = maxInput.currentDifference;
    out.mapping = mapTipToAngleWithCueItem(mapInput);
    if (!out.mapping.valid) return out;

    out.spinAngleDeg = out.mapping.angleDeg;
    out.spinFactor = out.combination.factor;
    out.valid = std::isfinite(out.spinAngleDeg) && std::isfinite(out.spinFactor);
    return out;
}

}} // namespace pp2::spin
