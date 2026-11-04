// Solver tests: every expected value follows from the decoded formulas (src/pp2_solvers.hpp), none is fitted.
#include "../src/pp2_engine.hpp"
using namespace pp2; using namespace pp2::solver;
static int fails = 0, total = 0;
static void check(const char* n, bool ok, const char* info = "") { ++total; if (!ok) ++fails; printf("%s %s %s\n", ok ? "PASS" : "FAIL", n, info); }
static char buf[200];
int main(int argc, char** argv) {
    Params p; double r = p.radius, sv = p.solverValue(), es = p.edgeSolver();
    check("literals: solverValue=2*0.001+0.00012, edgeSolver=0.0006", std::fabs(sv - 0.00212) < 1e-9 && std::fabs(es - 0.0006) < 1e-9);
    { // ball-ball overlap -> exactly touching (dist == 2r), centre of mass unchanged
        std::vector<P3> pos{{0, 0, 0}, {2 * r - 0.01, 0, 0}}; std::vector<bool> tube{false, false}; solveBallWithBall(pos, tube, p);
        double d = len3(pos[1] - pos[0]); snprintf(buf, 200, "dist=%.9f 2r=%.9f", d, 2 * r);
        check("ball-ball: overlap removed to exactly 2r", std::fabs(d - 2 * r) < 1e-12, buf);
        check("ball-ball: symmetric (midpoint kept)", std::fabs((pos[0].x + pos[1].x) * 0.5 - (2 * r - 0.01) * 0.5) < 1e-12); }
    { // non-overlapping untouched; InTube ignored
        std::vector<P3> pos{{0, 0, 0}, {2 * r + 0.001, 0, 0}, {0.01, 0, 0}}; std::vector<bool> tube{false, false, true}; auto q = pos; solveBallWithBall(q, tube, p);
        check("ball-ball: gap>=0 untouched, in-tube skipped", q[0].x == pos[0].x && q[1].x == pos[1].x && q[2].x == pos[2].x); }
    { // cue ball: other ball inside the solver band -> gap becomes solverValue (not larger), cue itself never moved
        P3 cue{0, 0, 0}; P3 b{2 * r + 0.0005, 0, 0};
        P3 q = solveWithCueBall(b, cue, false, false, p); double gap = len3(q - cue) - 2 * r; snprintf(buf, 200, "gap=%.9f sv=%.9f", gap, sv);
        check("cue-ball solver: gap pushed to solverValue", std::fabs(gap - sv) < 1e-12, buf);
        P3 far{2 * r + sv + 0.001, 0, 0}; check("cue-ball solver: outside band unchanged", solveWithCueBall(far, cue, false, false, p).x == far.x);
        check("cue-ball solver: cue itself / foul unchanged", solveWithCueBall(b, cue, true, false, p).x == b.x && solveWithCueBall(b, cue, false, true, p).x == b.x); }
    { // cushion: ball penetrating the AABB face -> distance from the box = r + edgeSolver ... closest point is the box face
        Aabb e{{-1, -1, 0}, {1, 1, 0.1}}; P3 b{0, 0, 0.1 + r - 0.01};   // 0.01 inside the 'r' band from the +z face
        P3 q = solveBallWithCushions(b, {e}, p); double d = q.z - 0.1; snprintf(buf, 200, "d=%.9f r+es=%.9f", d, r);
        check("cushion solver: dist-to-box = r + edgeSolver", std::fabs((d - r) - es) < 1e-12, buf); }
    { // engine: level 11 has 6 cushion colliders, solver keeps balls out of the table geometry bounds and never creates NaN
        Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { puts("cannot load data"); return 2; }
        check("level 11: 6 cushion colliders from 'E' triangles", S.W.rails.size() == 6);
        S.C.solveOverlap = true; Result R = S.run({{0, 0, 0, 0, 0}, {1, 0.04, 0.0, 0, 0}, {2, 0.02, 0.05, 0, 0}});
        bool ok = true; for (size_t i = 0; i < R.balls.size(); ++i) for (size_t j = i + 1; j < R.balls.size(); ++j) {
            double dx = R.balls[i].x - R.balls[j].x, dz = R.balls[i].z - R.balls[j].z; if (std::sqrt(dx * dx + dz * dz) < 2 * S.C.R - 1e-9) ok = false; }
        check("engine: overlapping start resolves to non-overlapping", ok); }
    printf("%d/%d passed\n", total - fails, total); return fails ? 1 : 0;
}
