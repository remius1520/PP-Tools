#include "../src/pp2_spin.hpp"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace pp2;
static int n=0, fail=0;
#define CHECK(c,m) do { ++n; if(!(c)){++fail; std::printf("FAIL %s line %d\n",m,__LINE__);} } while(0)
static bool near(double a,double b,double e=1e-8){return std::fabs(a-b)<=e;}
int main(){
    {
        spin::CueMappingInput i;
        i.horizontal=0.4; i.vertical=-0.2; i.currentDifference=2.0;
        i.intBallAngle=80; i.cueSpinFactorDividor=2.0;
        auto bound=spin::mapTipToAngleWithCueItem(i);
        spin::MappingInput reference; reference.horizontal=0.4; reference.vertical=-0.2;
        reference.currentDifference=2.0; reference.intBallAngle=80;
        reference.scaleX=2.0; reference.scaleY=2.0;
        auto expected=spin::mapTipToAngle(reference);
        CHECK(bound.valid && expected.valid,"CueItem divisor adapter produces a valid mapping");
        CHECK(near(bound.x,expected.x) && near(bound.y,expected.y),"same CueItem SpinFactorDividor feeds both ARM64 divisors");
        CHECK(near(bound.angleDeg,expected.angleDeg),"CueItem-bound mapping preserves angle and rounding");
        i.cueSpinFactorDividor=0.0;
        CHECK(!spin::mapTipToAngleWithCueItem(i).valid,"zero CueItem SpinFactorDividor rejected");
        i.cueSpinFactorDividor=std::numeric_limits<double>::quiet_NaN();
        CHECK(!spin::mapTipToAngleWithCueItem(i).valid,"non-finite CueItem SpinFactorDividor rejected");
    }
    {
        spin::MaxSpinGeometryInput i;
        i.vectorX=3.0; i.vectorY=4.0; i.vectorZ=12.0;
        i.cueBallRadius=2.0; i.currentDifference=45.0;
        i.zeroDifferenceMultiplication=0.1; i.radius=10.0;
        auto r=spin::calcMaxSpinGeometry(i);
        CHECK(r.valid,"max-spin geometry pre-pass valid");
        CHECK(near(r.vectorLength,13.0) && near(r.surfaceDistance,11.0),"geometry length subtracts cue-ball radius");
        CHECK(near(r.differenceWeight,0.5),"difference weight is currentDifference over 90 (0x42B40000 = 90.0f, was wrongly 180)");
        CHECK(near(r.radiusHeadroomWeight,0.55),"radius headroom uses radius minus difference offset");
        i.radius=1.0;
        r=spin::calcMaxSpinGeometry(i);
        CHECK(r.valid && near(r.radiusHeadroomWeight,0.0),"radius headroom clamps at zero");
        i.radius=1000.0;
        r=spin::calcMaxSpinGeometry(i);
        CHECK(r.valid && near(r.radiusHeadroomWeight,0.9955),"radius headroom approaches one");
        i.radius=0.0;
        CHECK(!spin::calcMaxSpinGeometry(i).valid,"zero radius rejected");
        i.radius=1.0; i.vectorX=std::numeric_limits<double>::infinity();
        CHECK(!spin::calcMaxSpinGeometry(i).valid,"non-finite geometry rejected");
    }
    {
        spin::MappingInput i; i.horizontal=0.4; i.vertical=-0.2; i.currentDifference=2.0;
        i.intBallAngle=80; i.scaleX=1; i.scaleY=1;
        auto r=spin::mapTipToAngle(i);
        CHECK(r.valid,"valid mapping");
        CHECK(near(r.kHorizontal,2) && near(r.kVertical,2),"below 3 uses 2x/2x multipliers");
        CHECK(near(r.x,3.6) && near(r.y,-3.8),"below 90 flips horizontal difference sign");
        CHECK(near(r.angleDeg,std::round(std::atan2(3.6,-3.8)*57.29577951308232*10)/10),"atan2(first=horizontal term, second=vertical term) and one-decimal rounding");
    }
    {
        spin::MappingInput i; i.horizontal=0.4; i.vertical=-0.2; i.currentDifference=4;
        i.intBallAngle=100; i.scaleX=2; i.scaleY=4;
        auto r=spin::mapTipToAngle(i);
        CHECK(r.valid,"second mapping valid");
        CHECK(near(r.kHorizontal,3) && near(r.kVertical,0.5),"at/above 3 uses 3x/0.5x multipliers");
        CHECK(near(r.x,-3.1) && near(r.y,-1.8/2),"above 90 keeps positive horizontal difference and uses separate scale divisors");
    }
    {
        spin::MappingInput i; i.currentDifference=0; i.horizontal=0; i.vertical=-1;
        i.scaleX=1; i.scaleY=1; i.fallbackAngleDeg=0.10000000149011612;
        auto r=spin::mapTipToAngle(i);
        CHECK(r.valid && near(r.angleDeg,0.1,1e-6),"rounded angle exactly 0 -> rodata constant 0.1 (not a field)");
    }
    {
        spin::MappingInput i; i.scaleX=0;
        auto r=spin::mapTipToAngle(i);
        CHECK(!r.valid,"zero transform scale rejected");
        i.scaleX=1; i.horizontal=std::nan("");
        CHECK(!spin::mapTipToAngle(i).valid,"non-finite tip coordinate rejected");
    }
    {
        spin::BiggerPointInput i; i.sourceBase=10; i.hitDistance=2; i.spinType=2;
        i.maxBaseIncreaseEdge=0.25; i.minBaseDecreaseRate=0.4; i.minBaseIncreaseSide=0.6;
        auto r=spin::calcMaxSpinBiggerPoint(i);
        CHECK(r.valid,"edge bigger-point valid");
        CHECK(near(r.selectedFactor,0.25),"spinType 2 selects maxBaseIncreaseEdge");
        CHECK(near(r.adjustedBase,8) && near(r.maxSpinBiggerPoint,10),"edge branch applies (base-hitDistance)*(1+factor)");
    }
    {
        spin::BiggerPointInput i; i.sourceBase=10; i.hitDistance=3; i.spinType=1;
        i.maxBaseIncreaseEdge=0.9; i.minBaseDecreaseRate=0.2; i.minBaseIncreaseSide=0.35;
        auto r=spin::calcMaxSpinBiggerPoint(i);
        CHECK(r.valid,"non-edge bigger-point valid");
        CHECK(near(r.selectedFactor,0.35),"non-edge selects maximum of two side factors");
        CHECK(near(r.maxSpinBiggerPoint,9.45),"non-edge branch output");
    }
    {
        spin::BiggerPointInput i; i.sourceBase=std::nan("");
        CHECK(!spin::calcMaxSpinBiggerPoint(i).valid,"non-finite bigger-point input rejected");
    }
    {
        spin::CircleNormalizationInput i; i.horizontal=-0.5; i.vertical=1.5;
        i.constRadius=2.0; i.currentDifference=0.5; i.zeroDifferenceMultiplication=1.0;
        auto r=spin::normalizeCircleInput(i);
        CHECK(r.valid,"circle normalization valid");
        CHECK(near(r.absHorizontal,0.5) && near(r.absVertical,1.5),"circle normalization uses absolute coordinates");
        CHECK(near(r.normalizedHorizontal,0.25) && near(r.normalizedVertical,0.75),"circle normalization divides by constRadius");
        CHECK(near(r.spinOffset,0.5),"spin offset is currentDifference times zero multiplier");
        CHECK(r.residualDefined && near(r.residualHorizontal,0.0) && near(r.residualVertical,2.0/3.0),"residual coordinates use offset-relative radius and clamp");
    }
    {
        spin::CircleNormalizationInput i; i.horizontal=0.25; i.vertical=-0.25;
        i.constRadius=1.0; i.currentDifference=1.0; i.zeroDifferenceMultiplication=1.0;
        auto r=spin::normalizeCircleInput(i);
        CHECK(r.valid && !r.residualDefined,"residual stage skipped when spin offset equals constRadius");
    }
    {
        spin::CircleNormalizationInput i; i.horizontal=0.4; i.vertical=0.6;
        i.constRadius=2.0; i.currentDifference=0.5; i.zeroDifferenceMultiplication=1.0;
        i.radius=4.0;
        auto r=spin::normalizeCircleInput(i);
        CHECK(r.valid,"directional radius input valid");
        CHECK(near(r.directionalRadiusWeight,0.875),"directional radius headroom uses third argument, not constRadius");
        i.radius=0.25;
        r=spin::normalizeCircleInput(i);
        CHECK(r.valid && near(r.directionalRadiusWeight,0.0),"directional radius headroom clamps below zero");
        i.radius=8.0;
        r=spin::normalizeCircleInput(i);
        CHECK(r.valid && near(r.directionalRadiusWeight,0.9375),"directional radius headroom scales with supplied radius");
    }
    {
        spin::CircleNormalizationInput i; i.radius=0.0;
        CHECK(!spin::normalizeCircleInput(i).valid,"zero third-argument radius rejected");
    }
    {
        spin::CircleNormalizationInput i; i.constRadius=0.0;
        CHECK(!spin::normalizeCircleInput(i).valid,"zero constRadius rejected");
        i.constRadius=1.0; i.horizontal=std::numeric_limits<double>::infinity();
        CHECK(!spin::normalizeCircleInput(i).valid,"non-finite circle input rejected");
    }

    {
        spin::CircleDirectionalInput i;
        i.circle.horizontal=0.8; i.circle.vertical=0.6;
        i.circle.constRadius=1.0; i.circle.currentDifference=0.2;
        i.circle.zeroDifferenceMultiplication=0.5; i.circle.radius=1.0;
        i.baseline=0.1; i.maxUp=0.9; i.maxLeft=0.3; i.maxRight=0.7; i.maxBottom=0.2;
        auto r=spin::divideCircleDirectional(i);
        CHECK(r.valid,"directional circle baseline valid");
        CHECK(near(r.selectedHorizontalMaximum,0.7) && near(r.selectedVerticalMaximum,0.9),"positive coordinates select right and up maxima");
        CHECK(near(r.horizontalValue,0.1+(0.7-0.1)*0.8),"horizontal interpolation uses normalized absolute coordinate");
        CHECK(near(r.verticalValue,0.1+(0.9-0.1)*0.6),"vertical interpolation uses normalized absolute coordinate");
    }
    {
        spin::CircleDirectionalInput i;
        i.circle.horizontal=0.5; i.circle.vertical=0.7; i.circle.constRadius=1.0;
        i.circle.currentDifference=0.2; i.circle.zeroDifferenceMultiplication=0.5; i.circle.radius=1.0;
        i.baseline=0.0; i.maxRight=0.8; i.maxLeft=0.6; i.maxUp=0.9; i.maxBottom=0.4;
        i.rightIncrement=-1.0; i.deadZone=0.2;
        auto r=spin::divideCircleDirectional(i);
        CHECK(r.valid && r.rightBranchScaled,"negative right increment activates right branch");
        CHECK(near(r.horizontalWeight,0.5/0.1) || near(r.horizontalWeight,1.0),"right branch uses offset-relative horizontal coordinate and clamps");
        CHECK(near(r.verticalWeight,0.5),"right branch applies vertical dead zone");
    }
    {
        spin::CircleDirectionalInput i;
        i.circle.horizontal=-0.5; i.circle.vertical=0.7; i.circle.constRadius=1.0;
        i.circle.currentDifference=0.2; i.circle.zeroDifferenceMultiplication=0.5; i.circle.radius=1.0;
        i.baseline=0.0; i.maxLeft=0.8; i.leftIncrement=-1.0; i.deadZone=0.2;
        auto r=spin::divideCircleDirectional(i);
        CHECK(r.valid && r.leftBranchScaled,"negative left increment activates left branch");
        CHECK(near(r.selectedHorizontalMaximum,0.8*(0.4/0.9)),"left maximum is scaled by residual horizontal coordinate");
    }
    {
        spin::CircleDirectionalInput i;
        i.circle.horizontal=0.7; i.circle.vertical=-0.5; i.circle.constRadius=1.0;
        i.circle.currentDifference=0.2; i.circle.zeroDifferenceMultiplication=0.5; i.circle.radius=1.0;
        i.baseline=0.0; i.maxBottom=0.8; i.bottomIncrement=-1.0; i.deadZoneVertical=0.2;
        auto r=spin::divideCircleDirectional(i);
        CHECK(r.valid && r.bottomBranchScaled,"negative bottom increment activates bottom branch");
        CHECK(near(r.selectedVerticalMaximum,0.8*(0.4/0.9)),"bottom maximum is scaled by residual vertical coordinate");
        CHECK(near(r.horizontalWeight,0.5),"bottom branch applies horizontal dead zone");
    }
    {
        spin::CircleDirectionalInput i; i.circle.radius=0.0;
        CHECK(!spin::divideCircleDirectional(i).valid,"directional stage rejects invalid radius");
        i.circle.radius=1.0; i.deadZone=std::nan("");
        CHECK(!spin::divideCircleDirectional(i).valid,"directional stage rejects non-finite limiter inputs");
    }

    {
        spin::ApplyCombinationInput i;
        i.spinType=2; i.vertical=2.0; i.constRadius=5.0; i.radius=1.0;
        i.directional.baseline=0.2; i.directional.maxUp=0.8;
        i.directional.maxBottom=0.4; i.directional.maxLeft=0.6; i.directional.maxRight=0.7;
        auto r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && r.edgeBranch,"spinType 2 edge combination valid");
        CHECK(near(r.normalizedVertical,1.0),"edge branch normalizes abs(vertical)/(constRadius-3)");
        CHECK(near(r.factor,0.8),"positive edge vertical interpolates to maxUp");
        i.vertical=-1.0;
        r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && near(r.normalizedVertical,0.5),"negative edge vertical uses absolute magnitude");
        CHECK(near(r.factor,0.0),"negative edge branch applies baseline plus negative maxBottom contribution then clamps");
    }
    {
        spin::ApplyCombinationInput i;
        i.spinType=0; i.horizontal=0.5; i.vertical=0.25; i.radius=1.0; i.constRadius=4.0;
        i.directional.circle.constRadius=1.0;
        i.directional.circle.currentDifference=0.0;
        i.directional.circle.zeroDifferenceMultiplication=0.0;
        i.directional.baseline=0.1;
        i.directional.maxUp=0.8; i.directional.maxRight=0.7;
        i.directional.maxLeft=0.4; i.directional.maxBottom=0.3;
        auto r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && !r.edgeBranch,"non-edge combination delegates to DivideTheCircle");
        CHECK(near(r.factor,0.1+(0.7-0.1)*0.5+(0.8-0.1)*0.25),"non-edge combination sums horizontal and vertical contributions minus baseline");
        i.horizontal=1.0; i.vertical=1.0;
        i.directional.maxRight=0.25; i.directional.maxUp=0.25;
        r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && near(r.factor,0.25),"non-edge result clamps to selected directional maximum");
    }
    {
        spin::ApplyCombinationInput i; i.constRadius=3.0;
        CHECK(!spin::applySpinFactorCombination(i).valid,"combination rejects degenerate constRadius minus three denominator");
        i.constRadius=4.0; i.radius=0.0;
        CHECK(!spin::applySpinFactorCombination(i).valid,"combination rejects non-positive radius");
    }

    {
        spin::MaxSpinCoefficientInput i;
        i.currentDifference=45.0; i.zeroDifferenceMultiplication=0.1; i.radius=10.0;
        i.globalBias=0.1; i.cueBallRadius=2.0; i.vectorX=3.0; i.vectorY=4.0; i.vectorZ=12.0;
        i.field130=0.8; i.field134=0.2; i.field138=0.1; i.field13C=0.5;
        i.field140=0.2; i.field144=0.6; i.field14C=0.3; i.field148=0.7;
        auto r=spin::calcMaxSpinCoefficientTerms(i);
        CHECK(r.valid,"CalcMaxSpinValues coefficient decomposition valid");
        CHECK(near(r.vectorLength,13.0) && near(r.surfaceDistance,11.0),"coefficient geometry uses vector length less cue-ball radius");
        CHECK(near(r.differenceWeight,0.5) && near(r.radiusHeadroomWeight,0.55),"coefficient decomposition reuses ARM64 clamped weights");
        CHECK(near(r.biasedDifferenceWeight,0.6),"global bias is added before clamp");
        CHECK(near(r.delta130_134,0.6) && near(r.term130_134,3.3),"130/134 delta is scaled by difference weight and surface distance");
        CHECK(near(r.delta138_13C,0.4) && near(r.term138_13C,0.24),"138/13C delta uses biased difference weight");
        CHECK(near(r.delta140_144,0.4) && near(r.term140_144,0.22),"140/144 delta uses radius headroom weight");
        CHECK(near(r.delta14C_148,0.4) && near(r.term14C_148,0.2),"14C/148 delta uses difference weight");
        i.globalBias=2.0; r=spin::calcMaxSpinCoefficientTerms(i);
        CHECK(r.valid && near(r.biasedDifferenceWeight,1.0),"biased difference weight clamps at one");
        i.radius=0.0; CHECK(!spin::calcMaxSpinCoefficientTerms(i).valid,"zero radius rejected");
        i.radius=10.0; i.vectorZ=std::numeric_limits<double>::infinity();
        CHECK(!spin::calcMaxSpinCoefficientTerms(i).valid,"non-finite transform vector rejected");
    }

    {
        spin::MaxSpinValuesInput i;
        i.radius=10.0; i.currentDifference=45.0; i.zeroDifferenceMultiplication=0.1;
        i.globalBias=0.1; i.externalBaseline=0.2; i.cueBallRadius=2.0;
        i.vectorX=3.0; i.vectorY=4.0; i.vectorZ=12.0;
        i.minBaseIncrementValue=0.8; i.maxBaseIncrementValue=0.2;
        i.minBaseDecreaseRate=0.1; i.maxBaseDecreaseRate=0.5;
        i.minBaseIncreaseAfterZero=0.2; i.maxBaseIncreaseAfterZero=0.6;
        i.minBaseIncreaseSide=0.1; i.maxBaseIncreaseSide=0.7;
        i.maxBaseIncreaseEdge=0.25; i.maxBaseDecreaseEdge=0.3;
        auto r=spin::calcMaxSpinValues(i);
        CHECK(r.valid,"full CalcMaxSpinValues reconstruction accepts finite source inputs");
        CHECK(near(r.vectorLength,13.0) && near(r.surfaceDistance,11.0),"full maxima use transform-vector length minus cue-ball radius");
        CHECK(near(r.differenceWeight,0.5) && near(r.biasedDifferenceWeight,0.6) && near(r.radiusHeadroomWeight,0.55),"full maxima reproduce the three ARM64 clamped weights");
        CHECK(near(r.incrementCandidate,0.5) && near(r.decreaseCandidate,0.34),"increment and decrease candidates follow serialized min/max fields");
        CHECK(near(r.afterZeroCandidate,0.42) && near(r.sideCandidate,0.4),"after-zero and side candidates interpolate in the ARM64 direction");
        CHECK(near(r.maxUp,5.7) && near(r.maxLeft,4.6) && near(r.maxRight,4.6) && near(r.maxBottom,5.7),"all four normal directional maxima match the disassembly branches (bottom = B+increment*surface, right = B+side*surface)");
        CHECK(near(r.maxAll,5.7),"maxAll is the maximum of four directional maxima");

        i.leftIncrement=-1.0; i.rightIncrement=-1.0; i.bottomIncrement=-1.0;
        r=spin::calcMaxSpinValues(i);
        CHECK(r.valid && near(r.maxLeft,4.62) && near(r.maxRight,4.62) && near(r.maxBottom,4.62),"negative directional increments select the shared headroom/decrease envelope");

        i.leftIncrement=0.0; i.rightIncrement=0.0; i.bottomIncrement=0.0; i.spinType=2;
        r=spin::calcMaxSpinValues(i);
        CHECK(r.valid && near(r.maxUp,0.25) && near(r.maxLeft,0.125) && near(r.maxBottom,0.06) && near(r.maxRight,0.03),"Edge mode derives Up/Left/Right/Bottom maxima with the observed half-value branches");
        i.radius=0.0;
        CHECK(!spin::calcMaxSpinValues(i).valid,"full CalcMaxSpinValues rejects zero radius");
        i.radius=10.0; i.externalBaseline=std::numeric_limits<double>::infinity();
        CHECK(!spin::calcMaxSpinValues(i).valid,"full CalcMaxSpinValues rejects non-finite external baseline");
    }
    {
        spin::ApplyCombinationInput i;
        i.spinType=2; i.vertical=-1.0; i.constRadius=5.0; i.radius=1.0;
        i.directional.baseline=0.2; i.directional.maxUp=0.8;
        i.directional.maxBottom=0.4; i.directional.maxLeft=0.6; i.directional.maxRight=0.7;
        auto r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && near(r.factor,0.0),"Edge negative branch subtracts t times maxBottom from baseline");
        i.vertical=1.0; r=spin::applySpinFactorCombination(i);
        CHECK(r.valid && near(r.factor,0.5),"Edge positive branch interpolates baseline toward maxUp");
    }

    {
        spin::SpinPipelineInput i;
        i.horizontal=0.2; i.vertical=0.1; i.radius=10.0; i.constRadius=5.0;
        i.mapping.intBallAngle=90; i.mapping.cueSpinFactorDividor=1.0;
        i.maxima.currentDifference=45.0; i.maxima.zeroDifferenceMultiplication=0.1;
        i.maxima.globalBias=0.1; i.maxima.externalBaseline=0.2; i.maxima.cueBallRadius=2.0;
        i.maxima.vectorX=3.0; i.maxima.vectorY=4.0; i.maxima.vectorZ=12.0;
        i.maxima.minBaseIncrementValue=0.8; i.maxima.maxBaseIncrementValue=0.2;
        i.maxima.minBaseDecreaseRate=0.1; i.maxima.maxBaseDecreaseRate=0.5;
        i.maxima.minBaseIncreaseAfterZero=0.2; i.maxima.maxBaseIncreaseAfterZero=0.6;
        i.maxima.minBaseIncreaseSide=0.1; i.maxima.maxBaseIncreaseSide=0.7;
        i.maxima.maxBaseIncreaseEdge=0.25; i.maxima.maxBaseDecreaseEdge=0.3;
        auto r=spin::evaluateSpinPipeline(i);
        CHECK(r.valid,"end-to-end tip-to-angle and tip-to-SpinFactor pipeline valid");
        CHECK(near(r.maxima.maxUp,5.7) && near(r.maxima.maxRight,4.6),"pipeline derives maxima before applying tip weights");
        CHECK(near(r.spinFactor,0.486),"pipeline combines horizontal and vertical spin contributions then subtracts baseline");
        CHECK(near(r.spinAngleDeg,-99.5,0.11),"pipeline derives rounded spin angle from the same tip coordinates");
        i.mapping.cueSpinFactorDividor=0.0;
        CHECK(!spin::evaluateSpinPipeline(i).valid,"pipeline rejects invalid cue divisor instead of inventing nullable default");
    }

    std::printf("%d checks, %d failed\n",n,fail); return fail?1:0;
}
