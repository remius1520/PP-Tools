// P2X: 3D convex-hull pocket rule (sphere vs true hull) -- consistency checks + difference vs the P2T prism rule on all 6 tables.
#include "../src/pp2_engine.hpp"
#include <cstdio>
#include <random>
using namespace pp2;
static int fails = 0, total = 0;
#define CHK(c) do { ++total; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data"; std::mt19937 rng(7);
    for (const auto& tp : kTables) {
        Sim S; if (!S.loadTable(dir, tp.key)) { printf("load fail %s\n", tp.key); return 1; }
        World& W = S.W; const double R = tp.R, Y = tp.Y; int withH3 = 0;
        for (auto& p : W.P) if (p.h3.size() >= 4) ++withH3; CHK(withH3 == 6);
        for (auto& p : W.P) {
            // hull sanity: outward normals, centroid inside, dist(centroid)=0, a point straight above the top face at height h is at distance h
            V3 c{0, 0, 0}; for (auto& t : p.h3) { c = add(c, t.a); } c = mul(c, 1.0 / p.h3.size());
            CHK(World::hullDist3(p.h3, c) == 0.0);
            double ymax = -1e9; for (auto& t : p.h3) ymax = std::max({ymax, t.a.y, t.b.y, t.c.y});
            for (size_t fi = 0; fi < p.h3.size(); fi += 7) {   // a point 1 cm outside a face (along its outward normal from the face centroid) is exactly 1 cm from the hull (convexity)
                const Tri& t = p.h3[fi]; V3 g = mul(add(add(t.a, t.b), t.c), 1.0 / 3), n = cross3(sub(t.b, t.a), sub(t.c, t.a)); double L = std::sqrt(dot3(n, n));
                CHK(std::fabs(World::hullDist3(p.h3, add(g, mul(n, 0.01 / L))) - 0.01) < 1e-9);
                CHK(World::hullDist3(p.h3, sub(g, mul(n, 0.0005 / L))) == 0.0);   // slightly inside
            }
            double xmax = -1e9; for (auto& t : p.h3) xmax = std::max({xmax, t.a.x, t.b.x, t.c.x});
            CHK(World::hullDist3(p.h3, {xmax + 0.02, c.y, c.z}) >= 0.02 - 1e-9);
            CHK(p.h3.size() >= 4 && p.lo.y <= ymax + 1e-9);
        }
        // 3D vs prism: random shots toward the pockets; entry times must agree closely, 3D never later than prism by more than the prism's own approximation
        double maxd = 0; int n = 0, missBoth = 0, only3 = 0, onlyP = 0;
        for (auto& p : W.P) for (int i = 0; i < 400; ++i) {
            std::uniform_real_distribution<double> U(-1, 1); double cx = 0.5 * (p.lo.x + p.hi.x), cz = 0.5 * (p.lo.z + p.hi.z);
            V2 o{cx + 0.35 * U(rng), cz + 0.35 * U(rng)}; double a = 3.14159265 * U(rng); V2 d{0.5 * std::cos(a), 0.5 * std::sin(a)};
            int w1, w2; double t3 = W.pocketEntry(o, d, R, Y, w1, true, true, true), tp2 = W.pocketEntry(o, d, R, Y, w2, true, true, false);
            if (t3 < 0 && tp2 < 0) { ++missBoth; continue; } if (t3 < 0) { ++onlyP; continue; } if (tp2 < 0) { ++only3; continue; }
            ++n; maxd = std::max(maxd, std::fabs(t3 - tp2) * 0.5);   // metres along the segment (|d|=0.5)
        }
        printf("%-8s R=%.4f Y=%.4f: compared %d shots, max |entry diff| = %.3f mm; only-3D=%d only-prism=%d neither=%d\n", tp.key, R, Y, n, maxd * 1000, only3, onlyP, missBoth);
        CHK(n > 100); CHK(maxd < 0.01);   // < 1 cm: the prism approximation is close but not identical
    }
    {   // packed binary round trip (tools/pack_data.py writes the HUL3 section)
        FILE* f = fopen("pp2_data_test.bin", "rb");
        if (f) { std::vector<unsigned char> b; unsigned char buf[65536]; size_t m; while ((m = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + m); fclose(f);
            World W2; CHK(W2.loadBinary(b.data(), b.size())); World W1; CHK(W1.load(dir)); CHK(W2.P.size() == W1.P.size());
            for (size_t i = 0; i < W1.P.size() && i < W2.P.size(); ++i) { CHK(W2.P[i].h3.size() == W1.P[i].h3.size() && W1.P[i].h3.size() >= 4); if (!W2.P[i].h3.empty()) CHK(std::fabs(W2.P[i].h3[3].c.y - W1.P[i].h3[3].c.y) < 1e-9); } }
        else printf("(no pp2_data_test.bin: binary round trip skipped)\n");
    }
    printf("%d checks, %d failed\n", total, fails); return fails != 0;
}
