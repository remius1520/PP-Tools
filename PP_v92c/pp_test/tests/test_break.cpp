#include "../core/suggest.hpp"
#include <cstdio>
using namespace pp;
static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++fails; } } while (0)
int main() {
    // table 11 from Table.zip/dump100_table11.txt + the live rack of table_dump--table 11.txt
    std::vector<V2> pk = {{-0.0010, 0.4284}, {-1.1447, 0.4090}, {1.1447, 0.4090}, {-0.0010, -0.7944}, {-1.1447, -0.7750}, {1.1447, -0.7750}};
    Table T = Table::fromPockets(pk, 0.055, 0.09, 0.07, 0.11, 0.07); Config C; C.drag = 0.62; C.drag1 = 1.10; C.vMax = 4.3; const double r = 0.0275;
    std::vector<BallIn> b = {{1, {0.4807, -0.1844}, r}, {2, {0.5291, -0.1513}, r}, {3, {0.5307, -0.2132}, r}, {4, {0.5832, -0.1244}, r}, {5, {0.5832, -0.1830}, r},
                             {6, {0.5832, -0.2410}, r}, {7, {0.6363, -0.1529}, r}, {8, {0.6363, -0.2101}, r}, {9, {0.6888, -0.1850}, r}};
    BreakPlanner P; P.begin(T, C, b, {-0.555, -0.174}, r);
    CHECK(P.candidates() >= 10, "candidates generated (spots + mirror images)");
    auto t0 = std::chrono::steady_clock::now(); int slices = 0; while (!P.step(2.0)) ++slices;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const BreakSuggestion& s = P.best();
    printf("      %d candidates, %.0f ms total (%d slices of 2 ms)\n", P.candidates(), ms, slices + 1);
    printf("      spot=(%.3f,%.3f) angle=%.2f power=%.2f exp=%.2f scratch=%.0f%% nine=%.0f%% legal=%.0f%%\n", s.spot.x, s.spot.z, s.angleDeg, s.power, s.expPotted, 100 * s.pScratch, 100 * s.pNine, 100 * s.pLegal);
    CHECK(s.ok, "a break is suggested");
    CHECK(s.spot.x < -0.48 && s.spot.x > -1.06, "cue spot is behind the head string and on the table");
    CHECK(s.pScratch < 0.01, "no scratch");
    CHECK(s.pLegal > 0.99, "legal break");
    CHECK(s.expPotted >= 1.5, "pots at least ~2 balls on average");
    Result R = P.replay(); bool hit1 = false; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 0 && t.end == END_BALL && t.hitId == 1) hit1 = true;
    CHECK(hit1, "replay: cue ball touches the apex ball first");
    // the same rack turned around (rack at the other end, cue on the other side): the planner must follow the rack, not the table
    std::vector<BallIn> m = b; for (auto& q : m) q.p.x = -q.p.x;
    BreakPlanner Q; Q.begin(T, C, m, {0.555, -0.174}, r); while (!Q.step(5.0)) {}
    CHECK(Q.best().ok && Q.best().spot.x > 0.48, "mirrored rack: spot is on the other side");
    // not a rack
    std::vector<BallIn> few = {b[0], b[1]}; BreakPlanner N; N.begin(T, C, few, {-0.5, 0}, r); CHECK(N.done() && !N.best().ok, "not a full rack = no break suggestion");
    printf(fails ? "FAILURES: %d\n" : "ALL PASSED (0 failures)\n", fails); return fails ? 1 : 0;
}
