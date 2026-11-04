// host test: bridge on a 15-ball break scene + a generic table check; prints tracks as JSON for the picture script
#include "../core/pp2_bridge.hpp"
#include <cstdio>
int main(int argc, char** argv) {
    pp::Pp2Bridge B; if (!B.ensure()) { puts("FAIL load"); return 1; }
    pp::Table T; for (auto& k : B.S.W.P) T.pockets.push_back({{0.5 * (k.lo.x + k.hi.x), 0.5 * (k.lo.z + k.hi.z)}, 0.05, 0.1, false});
    T.xmin = -1.2; T.xmax = 1.2; T.zmin = -0.8; T.zmax = 0.4;
    int fails = 0; auto chk = [&](bool c, const char* m) { printf("%s %s\n", c ? "PASS" : "FAIL", m); if (!c) ++fails; };
    chk(B.tableMatches(T), "table matches Level 11");
    pp::Table T2 = T; T2.pockets[0].p.x += 0.5; chk(!B.tableMatches(T2), "shifted table is rejected");
    std::vector<pp::BallIn> balls; const double r = 0.0275, d = 2 * r + 0.0005; int id = 1;
    for (int row = 0; row < 5; ++row) for (int k = 0; k <= row; ++k) balls.push_back({id++, {0.6 + row * d * std::sqrt(3.0) / 2, -0.18 + (k - row / 2.0) * d}, r});
    pp::Result R; double ang = argc > 1 ? atof(argv[1]) : 0.0;
    auto t0 = clock(); bool ok = B.run(T, balls, {-0.6, -0.18}, {std::cos(ang), std::sin(ang)}, 1.0, 4.3, R); double ms = 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
    chk(ok, "run ok"); printf("tracks=%zu finals=%zu time=%.1f ms\n", R.tracks.size(), R.finals.size(), ms);
    bool cueFirst = false; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 0 && t.pts.size() >= 2 && t.end == pp::END_BALL) cueFirst = true; chk(cueFirst, "cue main track ends on a ball");
    bool fin = true; for (auto& t : R.tracks) for (auto& p : t.pts) if (!std::isfinite(p.x) || !std::isfinite(p.z)) fin = false; chk(fin, "all points finite");
    FILE* f = fopen("/tmp/tracks.json", "w"); fprintf(f, "[");
    for (size_t i = 0; i < R.tracks.size(); ++i) { auto& t = R.tracks[i]; fprintf(f, "%s{\"id\":%d,\"gen\":%d,\"end\":%d,\"pts\":[", i ? "," : "", t.id, t.gen, (int)t.end); for (size_t k = 0; k < t.pts.size(); ++k) fprintf(f, "%s[%.5f,%.5f]", k ? "," : "", t.pts[k].x, t.pts[k].z); fprintf(f, "]}"); } fprintf(f, "]"); fclose(f);
    printf("%d fails\n", fails); return fails ? 1 : 0;
}
