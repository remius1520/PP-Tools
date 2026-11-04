#include "../src/pp2_scripted.hpp"
#include <cstdio>
using namespace pp2::scripted;
int f = 0; void chk(bool c, const char* m) { printf("%s %s\n", c ? "PASS" : "FAIL", m); if (!c) ++f; }
int main() {
    F3 r = edgeDir({1, 0, 0}, {-1, 0, 0}); chk(std::fabs(r.x + 0.92f) < 1e-6 && r.z == 0, "edge: head-on reflect x 0.92");
    r = edgeDir({1, 0, 1}, {0, 0, -1}); chk(std::fabs(r.x - 0.92f) < 1e-6 && std::fabs(r.z + 0.92f) < 1e-6, "edge: 45 deg keeps tangent*0.92");
    r = ballDir({2, 0, 0}, 0, 90, {-1, 0, 0}); chk(std::fabs(r.x - 2 * 0.92f) < 1e-5 && r.y == 0, "ball: sin90 -> |v|*0.92 along -n_xz");
    r = ballDir({2, 0, 0}, 0, 0, {-1, 0, 0}); chk(std::fabs(r.x) < 1e-6, "ball: angle 0 -> zero");
    r = ballDir({2, 0, 0}, 5, 90, {-1, 0, 0}); chk(std::fabs(r.x) < 1e-6, "ball: hitDist >= |v| -> zero");
    r = ballDir({2, 0, 0}, 0, 90, {0, 1, 0}); chk(r.x == 0 && r.z == 0, "ball: vertical normal -> no direction");
    chk(std::fabs(angleDegBetween({1, 0, 0}, {-1, 0, 0}) - 180.f) < 1e-3, "angle: opposite = 180");
    chk(std::fabs(angleDegBetween({0, 0, 1}, {1, 0, 0}) - 90.f) < 1e-3, "angle: perpendicular = 90");
    printf("%d fails\n", f); return f;
}
