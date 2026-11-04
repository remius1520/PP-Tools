#include "../core/suggest.hpp"
#include <cstdio>
using namespace pp;
static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++fails; } } while (0)
static Table mkTable() {
    std::vector<V2> pk = {{-1.30, -0.68}, {0, -0.71}, {1.30, -0.68}, {-1.30, 0.68}, {0, 0.71}, {1.30, 0.68}};
    return Table::fromPockets(pk, 0.055, 0.09, 0.07);
}
int main() {
    Table T = mkTable(); Config C; C.drag = 0.62; C.vMax = 4.3; const double r = 0.0285;
    // layout 1: balls 2,3,5,9 spread out, cue left
    std::vector<BallIn> b = {{2, {0.3, 0.2}, r}, {3, {-0.2, -0.35}, r}, {5, {0.7, -0.1}, r}, {9, {0.9, 0.4}, r}};
    Planner P; P.begin(T, C, b, {-0.8, 0.05}, r);
    CHECK(P.target() == 2, "target = lowest ball (2)");
    CHECK(P.candidates() > 100, "many candidates generated");
    auto t0 = std::chrono::steady_clock::now(); int steps = 0; while (!P.step(1.0)) ++steps;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("      planner finished: %d candidates, %.1f ms total, %d slices of 1 ms\n", P.candidates(), ms, steps + 1);
    const Suggestion& s = P.best();
    CHECK(s.ok, "a suggestion exists");
    printf("      kind=%d pocket=%d via=%d cush=%d power=%.2f angle=%.1f potted=%d tPot=%d robust=%d score=%.1f\n", s.kind, s.pocket, s.via, s.cushions, s.power, s.angleDeg, s.potted, (int)s.pottedTarget, s.robust, s.score);
    CHECK(s.kind != SK_NONE && s.target == 2, "suggestion targets the lowest ball");
    Result R = P.replay();
    const Track* ct = nullptr; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 0) { ct = &t; break; }
    CHECK(ct && ct->end == END_BALL && ct->hitId == 2, "replay: cue touches the lowest ball first");
    bool cuePk = false; for (auto& f : R.finals) if (f.id == -1 && f.pocketed) cuePk = true;
    CHECK(!cuePk, "replay: no scratch");
    // a straight pot is available here: ball 2 right in front of a corner pocket, the cue lined up. The planner must NOT answer with the direct shot.
    std::vector<BallIn> d = {{1, {1.1, 0.55}, r}, {9, {-0.9, -0.4}, r}};
    Planner Q; Q.begin(T, C, d, {0.6, 0.30}, r); while (!Q.step(5.0)) {}
    const Suggestion& q = Q.best(); printf("      direct-available layout: kind=%d cush=%d potted=%d score=%.1f\n", q.kind, q.cushions, q.potted, q.score);
    if (q.ok) { Result RQ = Q.replay(); bool direct = false; const Track* c2 = nullptr; for (auto& t : RQ.tracks) if (t.id == -1 && t.gen == 0) { c2 = &t; break; }
        bool t1rail = false; for (auto& t : RQ.tracks) if (t.id == 1 && !t.bounceIdx.empty()) t1rail = true; bool t1pot = false; for (auto& f : RQ.finals) if (f.id == 1 && f.pocketed) t1pot = true;
        direct = t1pot && c2 && c2->bounceIdx.empty() && !t1rail; CHECK(!direct, "direct pot is never suggested"); }
    // only the cue and one ball: still answers (or cleanly says no)
    std::vector<BallIn> one = {{9, {0.2, 0.1}, r}}; Planner S; S.begin(T, C, one, {-0.8, 0}, r); while (!S.step(5.0)) {}
    CHECK(true, "single ball layout finishes");
    // no balls: no crash
    std::vector<BallIn> none; Planner N; N.begin(T, C, none, {0, 0}, r); CHECK(N.done() && !N.best().ok, "empty table = no suggestion");
    printf(fails ? "FAILURES: %d\n" : "ALL PASSED (0 failures)\n", fails); return fails ? 1 : 0;
}
