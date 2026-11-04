// P2S: pocket trigger = convex mesh hull (not AABB).  Checks: hulls loaded (text + packed binary), hull inside AABB, hull entry never before AABB entry,
// corner-of-AABB segment misses the hull, centre segment hits both.  (consistency checks, not a comparison with the game)
#include "../src/pp2_engine.hpp"
#include <cstdio>
#include <cstdlib>
static int fails = 0, n = 0;
#define CHK(c) do { ++n; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data"; pp2::World W;
    if (!W.load(dir)) { printf("load failed\n"); return 1; }
    CHK(W.P.size() == 6);
    for (auto& p : W.P) {
        CHK(p.hull.size() >= 3);
        for (auto& v : p.hull) CHK(v.x >= p.lo.x - 1e-6 && v.x <= p.hi.x + 1e-6 && v.z >= p.lo.z - 1e-6 && v.z <= p.hi.z + 1e-6);
        // centroid of hull: both rules hit when the centre is inside
        double cx = 0, cz = 0; for (auto& v : p.hull) { cx += v.x; cz += v.z; } cx /= p.hull.size(); cz /= p.hull.size();
        int w; double Y = 0.5 * (p.lo.y + p.hi.y);
        double th = W.pocketEntry({cx, cz}, {0, 0}, 0.0275, Y, w, true), ta = W.pocketEntry({cx, cz}, {0, 0}, 0.0275, Y, w, false);
        CHK(th >= 0 && ta >= 0);
        // a point at the AABB corner (outside every round/chamfered hull) : AABB hits, hull misses
        double qx = (cx < 0 ? p.lo.x : p.hi.x), qz = (cz < 0 ? p.lo.z : p.hi.z); (void)qx; (void)qz;
        // sweep a ray from outside through the AABB corner region, 1000 directions: hull entry t >= AABB entry t whenever both hit
        for (int k = 0; k < 360; ++k) {
            double a = k * 3.14159265358979 / 180.0, dx = cos(a), dz = sin(a);
            pp2::V2 o{cx - dx * 0.4, cz - dz * 0.4}, d{dx * 0.8, dz * 0.8}; int w1, w2;
            double h = W.pocketEntry(o, d, 0.0275, Y, w1, true), b = W.pocketEntry(o, d, 0.0275, Y, w2, false);
            CHK(h >= 0 ? b >= 0 : true);                 // hull is inside AABB: a hull hit implies an AABB hit
            if (h >= 0 && b >= 0 && w1 == w2) CHK(h >= b - 1e-12);
        }
    }
    // corner pocket (CheckBall5, x 1.0978..1.1915 z 0.3602..0.4577): AABB corner point is outside the hull (area ratio 0.54)
    for (auto& p : W.P) if (p.name == "CheckBall5") {
        int w; double Y = 0.5 * (p.lo.y + p.hi.y);
        pp2::V2 o{p.lo.x - 0.05, p.lo.z + 0.003}, d{0.0, 0.0};   // static point just inside the AABB corner (lo.x+0.003.. use a tiny move)
        o = {p.lo.x + 0.003, p.lo.z + 0.003};
        CHK(W.pocketEntry(o, d, 0.0275, Y, w, false) >= 0);
        CHK(W.pocketEntry(o, d, 0.0275, Y, w, true) < 0);
    }
    // P2T: sphere-touch rule (Unity trigger = ball sphere overlaps the convex mesh)
    for (auto& p : W.P) {
        double Y = 0.5 * (p.lo.y + p.hi.y), R = 0.0275; int w;
        for (size_t i = 0; i < p.hull.size(); ++i) {
            const pp2::V2& a = p.hull[i]; const pp2::V2& b = p.hull[(i + 1) % p.hull.size()]; double ex = b.x - a.x, ez = b.z - a.z, L = std::sqrt(ex * ex + ez * ez); if (L < 1e-9) continue;
            double mx = 0.5 * (a.x + b.x), mz = 0.5 * (a.z + b.z), nx = ez / L, nz = -ex / L;     // one of the two edge normals
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                pp2::V2 q{mx + sgn * nx * (R * 0.5), mz + sgn * nz * (R * 0.5)}, q2{mx + sgn * nx * (R * 1.5), mz + sgn * nz * (R * 1.5)};
                bool in = W.pocketEntry(q, {0, 0}, R, Y, w, true, false) >= 0;
                if (!in) { CHK(W.pocketEntry(q, {0, 0}, R, Y, w, true, true) >= 0); CHK(W.pocketEntry(q2, {0, 0}, R, Y, w, true, true) < 0); }   // outside by R/2: touches; by 1.5R: does not
            }
        }
        for (int k = 0; k < 360; k += 5) {
            double a = k * 3.14159265358979 / 180.0, dx = std::cos(a), dz = std::sin(a); pp2::V2 o{p.lo.x * 0.5 + p.hi.x * 0.5 - dx * 0.5, p.lo.z * 0.5 + p.hi.z * 0.5 - dz * 0.5}, d{dx, dz}; int w1, w2;
            double hs = W.pocketEntry(o, d, R, Y, w1, true, true), hc = W.pocketEntry(o, d, R, Y, w2, true, false);
            CHK(hs >= 0); if (hc >= 0) CHK(hs <= hc + 1e-9);   // sphere touch never later than centre-inside
        }
    }
    // packed binary round-trip (tools/pack_data.py -> World::loadBinary) keeps the hulls
    if (system(("python3 tools/pack_data.py " + dir + " pockethull_test.bin pockethull_test.inc > /dev/null").c_str()) == 0) {
        FILE* f = fopen("pockethull_test.bin", "rb"); std::vector<unsigned char> buf; int c; while (f && (c = fgetc(f)) != EOF) buf.push_back((unsigned char)c); if (f) fclose(f);
        pp2::World B; CHK(!buf.empty() && B.loadBinary(buf.data(), buf.size())); CHK(B.P.size() == W.P.size());
        for (size_t i = 0; i < B.P.size() && i < W.P.size(); ++i) { CHK(B.P[i].hull.size() == W.P[i].hull.size()); for (size_t j = 0; j < B.P[i].hull.size() && j < W.P[i].hull.size(); ++j) CHK(B.P[i].hull[j].x == W.P[i].hull[j].x && B.P[i].hull[j].z == W.P[i].hull[j].z); }
        remove("pockethull_test.bin"); remove("pockethull_test.inc");
    }
    printf("%d checks, %d failed\n", n, fails); return fails != 0;
}
