// P2W: every table loads from its own files; rails/pockets counts; a ball shot straight at each pocket from the table centre is pocketed; a shot at the long cushion bounces back inside.
#include "../src/pp2_engine.hpp"
#include <cstdio>
using namespace pp2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL %s: ", t.key); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data"; int checks = 0;
    for (const auto& t : kTables) {
        Sim S; if (!S.loadTable(dir, t.key)) { printf("FAIL %s: load\n", t.key); ++fails; continue; }
        CHECK(S.W.P.size() == 6, "pockets %zu", S.W.P.size());
        CHECK(S.W.rails.size() == (std::string(t.key) == "lvl5_6" ? 8u : 6u), "rails %zu", S.W.rails.size());
        // P2W-merge: the frozen ball band [Y-R, Y+R] must overlap the cushion colliders (tag Edge, mat 'E') or the table's R/Y is wrong; 'E' count: Level13 Border boxes are untagged => 'K'
        { double ylo = 1e9, yhi = -1e9; for (auto& tr : S.W.T) if (tr.mat == 'E') for (const V3* v : {&tr.a, &tr.b, &tr.c}) { ylo = std::min(ylo, v->y); yhi = std::max(yhi, v->y); }
          CHECK(S.C.Y - S.C.R < yhi && S.C.Y + S.C.R > ylo, "ball Y band [%.4f,%.4f] misses cushions y[%.4f,%.4f]", S.C.Y - S.C.R, S.C.Y + S.C.R, ylo, yhi); }
        double minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9;
        for (auto& r : S.W.rails) { minx = std::min(minx, r.lo.x); maxx = std::max(maxx, r.hi.x); minz = std::min(minz, r.lo.z); maxz = std::max(maxz, r.hi.z); }
        double cx = 0.5 * (minx + maxx), cz = 0.5 * (minz + maxz);
        int sideOk = 0, sideN = 0, corner = 0;
        for (auto& p : S.W.P) {
            double sx = 0, sz = 0; for (auto& h : p.hull) { sx += h.x; sz += h.z; } V2 pc = p.hull.empty() ? V2{0.5 * (p.lo.x + p.hi.x), 0.5 * (p.lo.z + p.hi.z)} : V2{sx / p.hull.size(), sz / p.hull.size()};
            V2 d = pc - V2{cx, cz}; double L = std::sqrt(d.x * d.x + d.z * d.z); bool side = std::fabs(pc.x) < 0.3;
            std::vector<Ball> B{{0, cx, cz, 0, 0}}; Shot sh; sh.cueId = 0; sh.dx = d.x / L; sh.dz = d.z / L; sh.F = 2.0; sh.spin = -1;
            Result R = S.run(B, &sh, false); if (side) { ++sideN; sideOk += R.balls[0].pocketed; } else corner += R.balls[0].pocketed;
        }
        CHECK(sideN == 2 && sideOk == 2, "side pockets pocketed %d/%d", sideOk, sideN); ++checks;
        std::vector<Ball> B{{0, cx + 0.4, cz, 0, 0}}; Shot sh; sh.cueId = 0; sh.dx = 0; sh.dz = 1; sh.F = 1.0; sh.spin = -1;   // toward the +z cushion away from the side pocket
        Result R = S.run(B, &sh, false); int pocketed = sideOk;
        CHECK(!R.balls[0].pocketed && R.balls[0].z < maxz && R.balls[0].z > minz && R.wallHits >= 1, "cushion shot z=%.3f hits=%d pocketed=%d", R.balls[0].z, R.wallHits, (int)R.balls[0].pocketed); ++checks;
        printf("%-8s tris=%zu rails=%zu R=%.4f Y=%.4f(%s) side-pocketed=%d/2 (corner diagonals pocketed=%d, informational) cushion: wallHits=%d final z=%.3f\n", t.key, S.W.T.size(), S.W.rails.size(), t.R, t.Y, t.ySourced ? "sourced" : "ASSUMED", pocketed, corner, R.wallHits, R.balls[0].z);
    }
    printf("%d checks, %d failed\n", checks + 6 * 2, fails); return fails ? 1 : 0;
}
