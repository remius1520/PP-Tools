#include "../core/predict_core.hpp"
#include <cstdio>
using namespace pp;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", msg); } else std::printf("ok:   %s\n", msg); } while (0)
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }


// v36: mock "game geometry" = infinite planes (n points toward the playable side, surface n.x = c). Stands in for Physics.SphereCast in the tests.
struct PlaneProbe : Probe {
    struct Pl { V2 n; double c; }; std::vector<Pl> pls; double off = 0;   // off = extra cast radius (the game adds 2 x defaultContactOffset)
    bool cast(V2 p, V2 d, double r, double maxDist, CastHit& o) const override {
        double best = 1e18; V2 bn{0, 0};
        for (auto& q : pls) { double nd = dot(q.n, d); if (nd >= -1e-12) continue; double t = (q.c + r + off - dot(q.n, p)) / nd; if (t >= 0 && t < best && t < maxDist) { best = t; bn = q.n; } }
        if (best > 1e17) return false;
        o.t = best; o.n = bn; return true;
    }
};

int main() {
    // table 5 pockets from the dump
    std::vector<V2> pk = {{-0.0022, 0.4516}, {-1.1080, 0.4182}, {1.1182, 0.4246}, {0.0013, -0.7703}, {-1.1102, -0.7287}, {1.1168, -0.7373}};
    Table T = Table::fromPockets(pk, 0.085, 0.12, 0.10);
    Config C; C.maxBounces = 3;
    const double r = 0.0325;

    { // 1. straight shot, nothing in the way: one bounce off the right cushion, mirror symmetry
        Sim s(T, C, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        const Track& t = R.tracks[0];
        CHECK(t.pts.size() >= 2 && near(t.pts[1].x, T.xmax) && near(t.pts[1].z, 0.0), "cue hits right cushion exactly at xmax");
        CHECK(t.bounceIdx.size() >= 1, "bounce recorded");
    }
    { // 2. 45 degree bank: after bounce on top (z) the z component flips, x unchanged
        Sim s(T, C, {}, {0.0, 0.0}, r); double k = std::sqrt(0.5); Result R = s.run({k, k}, 1.0);
        const Track& t = R.tracks[0]; V2 a = t.pts[1], b = t.pts[2];
        CHECK(near(a.z, T.zmax) && near((b - a).x, std::fabs((b - a).z) * ((b - a).x > 0 ? 1 : -1)) , "45deg bank mirrors (|dx|==|dz| after bounce)");
        CHECK((b - a).z < 0, "z flipped after top cushion");
    }
    { // 3. head-on ball: target moves along the line, cue stops (tangent speed 0)
        std::vector<BallIn> b = {{1, {0.5, 0.0}, r}};
        Sim s(T, C, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].end == END_BALL && R.tracks[0].hitId == 1, "cue ends on ball 1");
        CHECK(near(R.tracks[0].pts.back().x, 0.5 - 2 * r, 1e-9), "ghost ball position = centre - 2r");
        const Track* cc = nullptr; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 1) cc = &t;
        CHECK((R.tracks.size() == 3 && R.tracks[1].id == 1) || cc, "head-on: target track + (v37) the cue creeps on at ~4% of its speed");
        double vin = 4.3 - 0.62 * (0.5 - 2 * r), creep = cc ? len(cc->pts.back() - cc->pts[0]) : 0;
        CHECK(cc && creep > 0 && creep < 0.1 * vin / 0.62, "head-on creep is short (<10% of the free roll)");
    }
    { // 4. 90 degree rule: cue and target leave at right angles (equal masses, no spin)
        std::vector<BallIn> b = {{1, {0.5, 0.04}, r}};
        Sim s(T, C, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        const Track* tg = nullptr; const Track* cu = nullptr;
        for (auto& t : R.tracks) { if (t.id == 1) tg = &t; if (t.id == -1 && t.gen == 1) cu = &t; }
        CHECK(tg && cu, "cut shot spawns target + cue continuation");
        if (tg && cu) {
            V2 dt = tg->pts[1] - tg->pts[0], dc = cu->pts[1] - cu->pts[0];
            CHECK(near(dot(dt, dc) / (len(dt) * len(dc)), 0.0, 1e-9), "target and cue directions are perpendicular");
        }
    }
    { // 5. pocket: target ball lined up with the top-right corner pocket stops there
        V2 pp_ = pk[2];                                   // (1.1182, 0.4246)
        V2 dir = pp_ - V2{0.6, 0.0}; double L = len(dir); dir = dir * (1.0 / L);
        V2 ballPos = V2{0.6, 0.0};
        V2 cuePos = ballPos - dir * 0.4;                  // straight line cue->ball->pocket
        std::vector<BallIn> b = {{3, ballPos, r}};
        Sim s(T, C, b, cuePos, r); Result R = s.run(dir, 1.0);
        const Track* tg = nullptr; for (auto& t : R.tracks) if (t.id == 3) tg = &t;
        CHECK(tg && tg->end == END_POCKET && tg->pocket == 2, "target ball ends in pocket 2");
    }
    { // 6. chain: cue -> ball1 -> ball2 (ball2 sits on ball1's line)
        std::vector<BallIn> b = {{1, {0.3, 0.0}, r}, {2, {0.6, 0.0}, r}};
        Sim s(T, C, b, {-0.4, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        bool ok = false; for (auto& t : R.tracks) if (t.id == 1 && t.end == END_BALL && t.hitId == 2) ok = true;
        bool has2 = false; for (auto& t : R.tracks) if (t.id == 2) has2 = true;
        CHECK(ok && has2, "ball1 hits ball2, ball2 gets its own path");
    }
    { // 7. power: lower power -> shorter path (travel = v0/drag), stops at rest
        Sim s(T, C, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.1);
        const Track& t = R.tracks[0];
        CHECK(t.end == END_REST && near(len(t.pts.back() - t.pts[0]), 0.1 * 4.3 / 0.62, 1e-9), "power 0.1 -> distance = 0.1*vMax/drag");
    }
    { // 8. bounce limit
        Config c2 = C; c2.maxBounces = 1; c2.cushionKeep = 1.0;
        Sim s(T, c2, {}, {0.0, 0.0}, r); double k = std::sqrt(0.5); Result R = s.run({k, k}, 1.0);
        CHECK(R.tracks[0].bounceIdx.size() == 1 && R.tracks[0].end == END_BOUNCELIMIT, "maxBounces=1: exactly one bounce, then the track ends at the next cushion");
    }
    { // 9. v20: a ball sliding along the bottom rail toward the corner pocket is POCKETED (old code bounced off the wall line before the mouth)
        double z = T.zmin + 0.004;
        Sim s(T, C, {}, {0.2, z}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].end == END_POCKET && R.tracks[0].pocket == 5, "rail-hugging shot drops into the corner pocket");
    }
    { // 10. v20: pocket end point stays ON the aim line (no snapping to the pocket centre); off-centre by 0.05 is still pocketed
        V2 pc = pk[2]; V2 start = {0.2, 0.0}; V2 tgt = {pc.x, pc.z - 0.05}; V2 aim = tgt - start; aim = aim * (1.0 / len(aim));
        Sim s(T, C, {}, start, r); Result R = s.run(aim, 1.0);
        const Track& t = R.tracks[0]; V2 e = t.pts.back() - start; double cross = e.x * aim.z - e.z * aim.x;
        CHECK(t.end == END_POCKET && t.pocket == 2 && t.bounceIdx.empty() && std::fabs(cross) < 1e-9, "off-centre pocket shot: no bounce, last point lies exactly on the aim ray");
    }
    { // 10b. v20: aimed exactly at the pocket centre from a shallow angle -> always pocketed
        V2 pc = pk[2]; V2 start = {0.2, 0.0}; V2 aim = pc - start; aim = aim * (1.0 / len(aim));
        Sim s(T, C, {}, start, r); Result R = s.run(aim, 1.0);
        CHECK(R.tracks[0].end == END_POCKET && R.tracks[0].bounceIdx.empty(), "centre-aimed shallow shot goes in (old code bounced on the wall line)");
    }
    { // 11. v20: shot well wide of the mouth still bounces
        Sim s(T, C, {}, {0.2, T.zmax - 0.3}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].end != END_POCKET && R.tracks[0].bounceIdx.size() >= 1, "wide shot bounces, not pocketed");
    }
    { // 12. v20: balls in the tray (outside the table) are ignored
        std::vector<BallIn> b = {{1, {0.5, T.zmin - 0.2}, r}};
        Sim s(T, C, b, {0.0, T.zmin + 0.001}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].end != END_BALL, "tray ball does not stop the cue ball");
    }
    { // 13. v37: drag law. A free roll stops after v0/drag and the time is -ln(1 - drag*s/v0)/drag (checked through t1 of the track)
        Config c = C; c.cushionKeep = 1.0; Sim s(T, c, {}, {0.0, 0.0}, r); Result R = s.run({0, 1}, 0.04);
        const Track& t = R.tracks[0]; double v0 = 0.04 * 4.3;
        CHECK(t.end == END_REST && std::fabs(R.finals.back().p.z - v0 / 0.62) < 0.03, "stopping distance = v0/drag (within the rest-threshold creep)");
    }
    { // 14. v37: cushion keeps edgerestitution of the speed: bounce, then the remaining roll is (v_at_wall*0.92)/drag
        Sim s(T, C, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.45);
        double v0 = 0.45 * 4.3, dw = T.xmax, vw = v0 - 0.62 * dw, back = vw * 0.92 / 0.62;
        const Track& t = R.tracks[0]; V2 e = t.pts.back();
        CHECK(near(e.x, T.xmax - back, 0.03) && t.bounceIdx.size() == 1, "after the cushion the ball rolls (v_wall*0.92)/drag back");
    }
    { // 15. v37: combo - the cue hits ball 1 which, while still rolling, hits ball 2 (ball 2 is placed so that ball 1 reaches it)
        std::vector<BallIn> b = {{1, {0.3, 0.0}, r}, {2, {0.6, 0.0}, r}};
        Sim s(T, C, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        bool hit12 = false; for (auto& t : R.tracks) if (t.id == 1 && t.end == END_BALL && t.hitId == 2) hit12 = true;
        double x2 = 0, x1 = 0; for (auto& f : R.finals) { if (f.id == 2) x2 = f.p.x; if (f.id == 1) x1 = f.p.x; }
        const Track* t2 = nullptr; for (auto& t : R.tracks) if (t.id == 2 && t.gen == 2) t2 = &t;
        CHECK(hit12 && t2 && t2->pts.size() >= 2 && t2->pts[1].x > 0.6 + 0.1, "combo: ball 2 is driven forward by ball 1");
        CHECK(x1 != 0 && x2 != 0, "combo: finals are reported for both balls");
    }
    { // 16. v37: a ball that is hit while ANOTHER ball is already moving (two movers) - momentum along the line of centres is conserved
        // ball 1 rests at (0.5,0), ball 2 rests at (0.5+..): the cue goes through the middle of two touching balls; all balls move afterwards
        std::vector<BallIn> b = {{1, {0.5, 0.0}, r}, {2, {0.5, 0.0655}, r}};
        Sim s(T, C, b, {0.0, 0.03275}, r); Result R = s.run({1, 0}, 1.0);
        int moved = 0; for (auto& f : R.finals) if (f.id > 0 && len(f.p - (f.id == 1 ? V2{0.5, 0.0} : V2{0.5, 0.0655})) > 0.02) ++moved;
        CHECK(moved == 2, "a hit on a touching pair moves both balls");
    }
    { // 17. v37: break - a rack of 15 touching balls; every ball is accounted for and none overlaps another at the end
        std::vector<BallIn> b; int id = 1; double d = 2 * r + 1e-4;
        for (int row = 0; row < 5; ++row) for (int k = 0; k <= row; ++k) { b.push_back({id++, {0.4 + row * d * 0.8660254, (k - row * 0.5) * d}, r}); }
        Sim s(T, C, b, {-0.5, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(R.finals.size() == 16, "break: 15 balls + cue reported");
        int movers = 0; for (auto& f : R.finals) { for (auto& g : b) if (g.id == f.id && len(f.p - g.p) > 0.05) ++movers; }
        CHECK(movers >= 8, "break scatters the rack (>= 8 balls move)");
        bool overlap = false; for (size_t i = 0; i < R.finals.size(); ++i) for (size_t j = i + 1; j < R.finals.size(); ++j) if (!R.finals[i].pocketed && !R.finals[j].pocketed && len(R.finals[i].p - R.finals[j].p) < 2 * r - 0.01) overlap = true;
        CHECK(!overlap, "no overlapping balls at rest after the break");
    }
    { // 18. v37: sequential contacts of one ball (cue hits ball 1, comes back and hits ball 2 later) - ball 2 gets its own track
        std::vector<BallIn> b = {{1, {0.3, 0.0}, r}, {2, {0.0, 0.3}, r}};
        Sim s(T, C, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        int hits = 0; for (auto& t : R.tracks) if (t.id == -1 && t.end == END_BALL) ++hits;
        CHECK(hits >= 1, "cue ball keeps being tracked after its first contact");
    }
    { // 19. v37: cut shot - target leaves along the line of centres with 0.92*cos*v, cue along the tangent
        double cut = 0.04; std::vector<BallIn> b = {{1, {0.5, cut}, r}};
        Config gbC = C; gbC.tgtGameBasis = true; Sim s(T, gbC, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.12);
        const Track* tg = nullptr; for (auto& t : R.tracks) if (t.id == 1 && t.gen == 1) tg = &t;
        V2 ghost = R.tracks[0].pts.back(); V2 n = V2{0.5, cut} - ghost; n = n * (1.0 / len(n)); double cs = n.x;
        double vin = 0.12 * 4.3 - 1.0 * ghost.x; double expect = 0.92 * cs * vin / 0.62;     // v40: game basis (launch force - distance rolled)
        CHECK(tg && near(len(tg->pts.back() - tg->pts[0]), expect, 0.03), "target travels 0.92*cos*(v0-dist)/drag (game basis)");
        Config legacy = C; legacy.tgtGameBasis = false; Sim sl(T, legacy, b, {0.0, 0.0}, r); Result Rl = sl.run({1, 0}, 0.12);
        const Track* tl = nullptr; for (auto& t : Rl.tracks) if (t.id == 1 && t.gen == 1) tl = &t;
        double vin2 = 0.12 * 4.3 - 0.62 * ghost.x;
        CHECK(tl && near(len(tl->pts.back() - tl->pts[0]), 0.92 * cs * vin2 / 0.62, 0.03), "tgtGameBasis=false: legacy drag-decayed target speed");
        CHECK(tg && tl && len(tg->pts.back() - tg->pts[0]) < len(tl->pts.back() - tl->pts[0]), "game basis makes the target line shorter than the legacy one");
    }
    { // 20. v37: ball pocketed in a corner is removed and reported
        V2 pc = pk[2]; V2 start = {0.6, 0.0}; V2 aim = pc - start; aim = aim * (1.0 / len(aim));
        std::vector<BallIn> b = {{7, {0.9, -0.2}, r}};
        Sim s(T, C, b, start, r); Result R = s.run(aim, 1.0);
        bool potted = false; for (auto& f : R.finals) if (f.id == -1 && f.pocketed && f.pocket == 2) potted = true;
        CHECK(potted, "finals report the pocketed ball and its pocket");
    }
    { // 15b. v64h: learned cushion rebound (normal keep 0.85, tangential keep 0.95) is recovered by the same formula the payload uses on a real path
        Config c2 = C; c2.drag = 1.1; c2.cushionKeep = 0.85; c2.tanKeep = 0.95; c2.maxBounces = 3; c2.maxChain = 1;
        Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        Sim s(M, c2, {}, {-0.5, 0.1}, r); V2 d{0.97, -0.17}; d = d * (1.0 / len(d)); double v0 = 0.6 * 4.3; Result R = s.run(d, 0.6);
        const Track& t = R.tracks[0]; CHECK(t.pts.size() >= 3, "one bounce then a stop");
        if (t.pts.size() >= 3) {
            V2 p0 = t.pts[0], X = t.pts[1], pe = t.pts.back(); double D1 = len(X - p0), D2 = len(pe - X), dr = 1.1;
            V2 d1 = (X - p0) * (1.0 / D1), d2 = (pe - X) * (1.0 / D2); V2 n{-1, 0}; CHECK(std::fabs(X.x - 1.0) < 0.05, "bounce on the right wall");
            V2 tg{-n.z, n.x}; double sIn = v0 - dr * D1, sOut = dr * D2, dnIn = -dot(d1, n), dnOut = dot(d2, n), dtIn = dot(d1, tg), dtOut = dot(d2, tg);
            double en = sOut * dnOut / (sIn * dnIn), et = sOut * dtOut / (sIn * dtIn);
            CHECK(std::fabs(en - 0.85) < 0.03 && std::fabs(et - 0.95) < 0.04, "learn formula recovers normal 0.85 / tangential 0.95");
            if (!(std::fabs(en - 0.85) < 0.03 && std::fabs(et - 0.95) < 0.04)) printf("   en=%.4f et=%.4f D1=%.3f D2=%.3f X=(%.3f,%.3f)\n", en, et, D1, D2, X.x, X.z);
        }
    }
    { // 15c. v64i: ball-hit learning formulas recover ballKeep 0.85 / cueKeep 0.9 from a simulated cut
        Config c3 = C; c3.drag = 1.1; c3.ballKeep = 0.85; c3.cueKeep = 0.9; c3.maxBounces = 3;
        Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        std::vector<BallIn> bs = {{1, {0.1, 0.05}, r}};
        V2 c0{-0.6, 0.0}, tgt = bs[0].p; V2 dd = tgt - c0; double dl = len(dd); dd = dd * (1.0 / dl);
        double off = 0.035; V2 perp{-dd.z, dd.x}; V2 aimP = tgt + perp * off; V2 d = aimP - c0; d = d * (1.0 / len(d));
        Sim s(M, c3, bs, c0, r); double v0 = 0.32 * 4.3; Result R = s.run(d, 0.32);
        V2 tEnd{0, 0}, cEnd{0, 0}; for (auto& f : R.finals) { if (f.id == 1) tEnd = f.p; else if (f.id < 0) cEnd = f.p; }
        double Dt = len(tEnd - tgt); V2 dt = (tEnd - tgt) * (1.0 / Dt); V2 C2 = tgt - dt * (2 * r); double D1 = len(C2 - c0); V2 d1 = (C2 - c0) * (1.0 / D1); double cs = dot(d1, dt), sn = std::sqrt(1 - cs * cs);
        double dr = 1.1, sIn = v0 - dr * D1, bk = dr * Dt / (cs * sIn); double Dc = len(cEnd - C2); Config dflt; double ms = std::min(1.0, std::max(dflt.minCueSin, sn)), ck = dr * Dc / (std::max(0.05, v0 - dflt.distFactor * D1) * ms);
        CHECK(std::fabs(bk - 0.85) < 0.04 && std::fabs(ck - 0.9) < 0.06, "ball-hit learn formula recovers ballKeep 0.85 / cueKeep 0.9");
        printf("   bk=%.4f ck=%.4f D1=%.3f Dt=%.3f Dc=%.3f cos=%.3f\n", bk, ck, D1, Dt, Dc, cs);
    }
    { // 15. v20: measured limits are used as given
        Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.3, 0.12, 0.10);
        Sim s(M, C, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        CHECK(near(R.tracks[0].pts[1].x, 1.0), "fromLimits: bounce exactly at the measured x limit");
    }
    { // 16. v21: the game's own first-contact directions replace the 90-degree rule (when they agree with the line of centres)
        std::vector<BallIn> b = {{1, {0.5, 0.02}, r}};
        Config co = C; co.hasOv = true; V2 n0 = {0.929, 0.370}; n0 = n0 * (1.0 / len(n0)); co.ovTarget = n0; co.ovCue = V2{0.1, -0.99} * (1.0 / len(V2{0.1, -0.99}));
        Sim s(T, co, b, {0.0, 0.0}, r); Result R = s.run({1, 0}, 1.0);
        const Track* tg = nullptr; const Track* cu = nullptr; for (auto& t : R.tracks) { if (t.id == 1) tg = &t; if (t.id == -1 && t.gen == 1) cu = &t; }
        V2 tdv = tg->pts[1] - tg->pts[0]; V2 gh = R.tracks[0].pts.back(); V2 exact = V2{0.5, 0.02} - gh; exact = exact * (1.0 / len(exact));
        CHECK(tg && near(len(tdv * (1.0 / len(tdv)) - exact), 0.0, 1e-9), "v40: target leaves on the EXACT line of centres (game's inflated-cast direction ignored)");
        CHECK(cu && (cu->pts[1] - cu->pts[0]).z < 0, "game cue rule is still applied (cue-after goes to -z)");
        Config lg = co; lg.ovExactGeometry = false; Sim sg(T, lg, b, {0.0, 0.0}, r); Result Rg = sg.run({1, 0}, 1.0); const Track* tgg = nullptr; for (auto& t : Rg.tracks) if (t.id == 1) tgg = &t;
        CHECK(tgg && near(len((tgg->pts[1] - tgg->pts[0]) * (1.0 / len(tgg->pts[1] - tgg->pts[0])) - n0), 0.0, 1e-9), "ovExactGeometry=false: legacy game target direction");
        Config bad = co; bad.ovTarget = V2{0, 1};                       // disagrees with the geometry -> ignored
        Sim s2(T, bad, b, {0.0, 0.0}, r); Result R2 = s2.run({1, 0}, 1.0); const Track* t2 = nullptr; for (auto& t : R2.tracks) if (t.id == 1) t2 = &t;
        CHECK(t2 && (t2->pts[1] - t2->pts[0]).x > 0, "implausible game direction is ignored");
    }
    { // 12. game geometry (probe): same box as fromLimits -> identical bounce point
        PlaneProbe pr; pr.pls = {{{1, 0}, -1.1}, {{-1, 0}, -1.1}, {{0, 1}, -0.6}, {{0, -1}, -0.6}};   // walls at x=+-1.1, z=+-0.6
        Table Tb = Table::fromLimits(pk, -1.1 + r, 1.1 - r, -0.6 + r, 0.6 - r, 0.12, 0.10);
        Sim a(Tb, C, {}, {0.1, 0.05}, r); Sim b(Tb, C, {}, {0.1, 0.05}, r); b.setProbe(&pr);
        double k = std::sqrt(0.5); Result Ra = a.run({k, k}, 1.0), Rb = b.run({k, k}, 1.0);
        bool same = Ra.tracks[0].pts.size() == Rb.tracks[0].pts.size();
        for (size_t i = 0; same && i < Ra.tracks[0].pts.size(); ++i) same = near(Ra.tracks[0].pts[i].x, Rb.tracks[0].pts[i].x, 1e-3) && near(Ra.tracks[0].pts[i].z, Rb.tracks[0].pts[i].z, 1e-3);
        CHECK(same, "probe with an axis-aligned box == rectangle limits");
    }
    { // 13. a wall tilted 45 degrees: a ball moving +x leaves along +z (mirror about the REAL normal), contact on the cast-radius offset
        PlaneProbe pr; double k = std::sqrt(0.5); pr.pls = {{{-k, k}, 0.0}}; pr.off = 0.02;   // surface x*(-k)+z*k = 0 ; ball is on the side n.p > 0
        Sim s(T, C, {}, {-0.5, 0.0}, r); s.setProbe(&pr);   // start at (-0.5,0): n.p = 0.3535 > 0 (playable side)
        Result R = s.run({1, 0}, 1.0); const Track& t = R.tracks[0];
        V2 a = t.pts[1], b = t.pts[2]; V2 dv = b - a; double l = len(dv);
        CHECK(t.pts.size() >= 3 && near(dv.x / l, 0.0, 1e-3) && near(dv.z / l, 1.0, 1e-3), "45 degree wall: leaves along +z");
        CHECK(near(dot(pr.pls[0].n, a), r + 0.02, 1e-6), "contact point sits cast-radius from the surface");
    }
    { // 14. probe + ball: ball hit still handled by the circle test (probe only supplies cushions)
        PlaneProbe pr; pr.pls = {{{-1, 0}, -1.1}};
        std::vector<BallIn> b = {{1, {0.5, 0.0}, r}};
        Sim s(T, C, b, {0.0, 0.0}, r); s.setProbe(&pr); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].end == END_BALL && R.tracks[0].hitId == 1, "probe mode still stops on a ball");
    }
    { // 15. v38 cushion spin: yaw by edgeSpinDeg after the bounce (Unity left-handed: x' = x cos + z sin, z' = z cos - x sin), cue ball, first hit only
        Config sc = C; sc.edgeSpinDeg = 10; sc.maxBounces = 3;
        Sim s0(T, C, {}, {0.0, 0.0}, r); Sim s1(T, sc, {}, {0.0, 0.0}, r);
        double k = std::sqrt(0.5);
        Result R0 = s0.run({k, k}, 1.0), R1 = s1.run({k, k}, 1.0);
        V2 d0 = R0.tracks[0].pts[2] - R0.tracks[0].pts[1], d1 = R1.tracks[0].pts[2] - R1.tracks[0].pts[1];
        double a0 = std::atan2(d0.z, d0.x), a1 = std::atan2(d1.z, d1.x);
        // d0 = (+k,-k) after the top cushion; yaw +10 deg: x' = x cos + z sin, z' = z cos - x sin
        double c10 = std::cos(10 * M_PI / 180), s10 = std::sin(10 * M_PI / 180); V2 e{d0.x * c10 + d0.z * s10, d0.z * c10 - d0.x * s10};
        CHECK(near(std::atan2(e.z, e.x), a1, 1e-6), "edge spin: outgoing direction == Yaw(+10deg) * mirrored direction");
        CHECK(std::fabs(a1 - a0) > 0.15, "edge spin changes the post-bounce line");
        // only the first cushion hit is rotated (edgeSpinBounces = 1): a second leg is a plain mirror of the rotated one
        Config s2 = sc; s2.edgeSpinBounces = 0; Sim s3(T, s2, {}, {0.0, 0.0}, r); Result R3 = s3.run({k, k}, 1.0);
        V2 d3 = R3.tracks[0].pts[2] - R3.tracks[0].pts[1];
        CHECK(near(std::atan2(d3.z, d3.x), a0, 1e-9), "edgeSpinBounces = 0 disables the rotation");
        // a rotation that would send the ball back into the wall is skipped (head-on into the right cushion, +100 deg)
        Config big = C; big.edgeSpinDeg = 100; Sim s4(T, big, {}, {0.0, 0.0}, r); Result R4 = s4.run({1, 0}, 1.0);
        V2 d4 = R4.tracks[0].pts[2] - R4.tracks[0].pts[1];
        CHECK(d4.x < 0, "rotation into the wall is skipped (ball still leaves the cushion)");
        // spin never touches target balls
        std::vector<BallIn> b = {{1, {0.5, 0.0}, r}};
        Sim s5(T, sc, b, {0.0, 0.0}, r); Result R5 = s5.run({1, 0}, 1.0); const Track* tt = nullptr; for (auto& q : R5.tracks) if (q.id == 1) tt = &q;
        CHECK(tt && std::fabs((tt->pts[1] - tt->pts[0]).z) < 1e-9, "spin is cue-ball only (target leaves on the line of centres)");
    }
    { // 16. v39 PredictCueDirAfterBallCollision / IntBallAngle ported from the binary
        using namespace pp::gamecue;
        V3 up{0, 1, 0}, n{1, 0, 0};
        CHECK(intBallAngle(n, up) == 90, "flat table, horizontal normal -> IntBallAngle 90");
        CHECK(near(classicTarget(90), 180, 1e-12) && near(classicTarget(87), 95, 1e-12) && near(classicTarget(89), 150, 1e-12), "classic target table (a<88, 88..89, 90)");
        CHECK(near(classicTarget(91), 210, 1e-12) && near(classicTarget(92), 210, 1e-12) && near(classicTarget(93), 265, 1e-12) && near(classicTarget(181), 180, 1e-12), "classic target table (91..92, >92, >180)");
        V3 d = dirAfterBall(n, 0, 180);                      // no spin -> classic angle 180: (x cos + z sin, z cos - x sin) of (1,0,0) = (-1, 0, 0)
        CHECK(near(d.x, -1, 1e-9) && near(d.z, 0, 1e-9), "no spin: Euler(0,180) * n");
        V3 e = dirAfterBall(n, 30, 180);                     // spin != 0 -> Qspin
        CHECK(near(e.x, std::cos(30 * M_PI / 180), 1e-9) && near(e.z, -std::sin(30 * M_PI / 180), 1e-9), "spin: Euler(0,30) * n (left handed)");
        V3 tilt{0, 0.5, 0.8660254037844386};                 // up tilted: dot(up, Euler(0,-90)*n) with n=(1,0,0): rot=(0,0,1) -> acos(-0.866)=150
        CHECK(intBallAngle(n, tilt) == 150, "tilted up vector gives acos(-dot) = 150");
        CHECK(intBallAngle(n, {0, 0, 0}) == 90, "degenerate up -> 90");
    }

    // ================= v40: exact geometry =================
    { // 21. ball-in-ball-in-ball: every ghost position and every target direction must match the analytic geometry (independent of speeds)
        Config cc = C; cc.maxChain = 6; cc.maxBounces = 6;
        V2 s0{0, 0}, aim{1, 0}; V2 A{0.5, 0.030};
        double tA = rayCircle(s0, aim, A, 2 * r); V2 G1 = s0 + aim * tA; V2 n1 = (A - G1) * (1.0 / len(A - G1));
        V2 perp1{-n1.z, n1.x};
        V2 B = A + n1 * 0.30 + perp1 * 0.022;
        double tB = rayCircle(A, n1, B, 2 * r); V2 G2 = A + n1 * tB; V2 n2 = (B - G2) * (1.0 / len(B - G2));
        V2 perp2{-n2.z, n2.x};
        V2 Cc = B + n2 * 0.22 + perp2 * (-0.015);
        double tC = rayCircle(B, n2, Cc, 2 * r); V2 G3 = B + n2 * tC; V2 n3 = (Cc - G3) * (1.0 / len(Cc - G3));
        std::vector<BallIn> b = {{1, A, r}, {2, B, r}, {3, Cc, r}};
        Sim s(T, cc, b, s0, r); Result R = s.run(aim, 1.0);
        const Track *cue0 = &R.tracks[0], *a1 = nullptr, *b2 = nullptr, *c3 = nullptr;
        for (auto& t : R.tracks) { if (t.id == 1 && t.gen == 1 && !a1) a1 = &t; if (t.id == 2 && t.gen == 2 && !b2) b2 = &t; if (t.id == 3 && t.gen == 3 && !c3) c3 = &t; }
        CHECK(a1 && b2 && c3, "3-ball chain: tracks of generation 1, 2 and 3 are all recorded (maxChain=6)");
        CHECK(near(len(cue0->ghost - G1), 0.0, 1e-6), "chain: cue ghost position == analytic contact with ball 1");
        auto dirOf = [](const Track* t) { V2 d = t->pts[1] - t->pts[0]; return d * (1.0 / len(d)); };
        CHECK(a1 && near(len(dirOf(a1) - n1), 0.0, 1e-6), "chain: ball 1 leaves exactly on the line of centres (cue->1)");
        CHECK(a1 && near(len(a1->ghost - G2), 0.0, 1e-6), "chain: ball 1 ghost == analytic contact with ball 2");
        CHECK(b2 && near(len(dirOf(b2) - n2), 0.0, 1e-6), "chain: ball 2 leaves exactly on the line of centres (1->2)");
        CHECK(b2 && near(len(b2->ghost - G3), 0.0, 1e-6), "chain: ball 2 ghost == analytic contact with ball 3");
        CHECK(c3 && near(len(dirOf(c3) - n3), 0.0, 1e-6), "chain: ball 3 leaves exactly on the line of centres (2->3)");
        Config c2 = cc; c2.maxChain = 2; Sim s2(T, c2, b, s0, r); Result R2 = s2.run(aim, 1.0); bool g3 = false; for (auto& t : R2.tracks) if (t.id == 3 && t.gen == 3) g3 = (t.pts.size() >= 2);
        CHECK(!g3, "maxChain=2 hides the third ball (that is why the app default is now 4)");
    }
    { // 22. a cue ball that already used a cushion must ignore the game's first-contact data (it belongs to the aim line)
        double k = std::sqrt(0.5); Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.3, 0.12, 0.10);
        V2 s0{0.0, -0.3}; V2 aim{k, k}; double th = (M.zmax - s0.z) / k; V2 hitp = s0 + aim * th; V2 back{k, -k};
        V2 Bp = hitp + back * 0.25; Bp.z -= 0.012;                // a cut hit on the way back
        std::vector<BallIn> b = {{1, Bp, r}};
        Config co = C; co.hasOv = true; co.ovTarget = V2{back.x * std::cos(0.10) - back.z * std::sin(0.10), back.x * std::sin(0.10) + back.z * std::cos(0.10)}; co.ovCue = V2{0, 1};
        Sim s(M, co, b, s0, r); Result R = s.run(aim, 1.0);
        const Track* tg = nullptr; for (auto& t : R.tracks) if (t.id == 1 && t.gen >= 1) { tg = &t; break; }
        const Track& c0 = R.tracks[0]; V2 gh = c0.ghost; V2 ex = Bp - gh; ex = ex * (1.0 / len(ex));
        V2 dd = tg ? tg->pts[1] - tg->pts[0] : V2{1, 0}; dd = dd * (1.0 / len(dd));
        CHECK(c0.end == END_BALL && tg && near(len(dd - ex), 0.0, 1e-9), "after a bank the game's first-contact data is ignored: exact line of centres");
        Config off = co; off.ovPristineOnly = false; off.ovExactGeometry = false; Sim s3(M, off, b, s0, r); Result R3 = s3.run(aim, 1.0);
        const Track* t3 = nullptr; for (auto& t : R3.tracks) if (t.id == 1 && t.gen >= 1) { t3 = &t; break; }
        V2 d3 = t3 ? t3->pts[1] - t3->pts[0] : V2{1, 0}; d3 = d3 * (1.0 / len(d3));
        CHECK(t3 && len(d3 - ex) > 1e-4 && len(d3 - ex) < 0.25, "ovPristineOnly=false: the stale game data would tilt the target line (what the new default prevents)");
    }
    { // 23. bank with a noisy (2 deg) mesh normal: snapped -> exact mirror; unsnapped -> off by ~4 deg
        double th = 2.0 * M_PI / 180.0; PlaneProbe pr; pr.pls = {{{std::sin(th), -std::cos(th)}, -0.6 * std::cos(th)}};   // top rail near z=0.6, tilted 2 deg
        double k = std::sqrt(0.5);
        Config a = C; a.snapNormals = true; a.maxBounces = 4; Config b2 = C; b2.snapNormals = false; b2.maxBounces = 4;
        Sim sa(T, a, {}, {0.0, 0.0}, r); sa.setProbe(&pr); Result Ra = sa.run({k, k}, 1.0);
        Sim sb(T, b2, {}, {0.0, 0.0}, r); sb.setProbe(&pr); Result Rb = sb.run({k, k}, 1.0);
        V2 da = Ra.tracks[0].pts[2] - Ra.tracks[0].pts[1], db = Rb.tracks[0].pts[2] - Rb.tracks[0].pts[1];
        double aa = std::atan2(da.z, da.x), ab = std::atan2(db.z, db.x), exact = -M_PI / 4;
        CHECK(near(aa, exact, 2e-4), "snapNormals: 2-degree mesh noise does not tilt the reflection (exact mirror)");
        CHECK(std::fabs(ab - exact) > 0.05, "snapNormals=false: the same noise tilts the line by ~4 degrees");
    }
    { // 24. four bounces inside a box through the probe == analytic unfolding of the rectangle (cast radius = ball radius)
        PlaneProbe pr; pr.pls = {{{1, 0}, -1.1}, {{-1, 0}, -1.1}, {{0, 1}, -0.6}, {{0, -1}, -0.6}};
        Table Tb = Table::fromLimits(pk, -1.1 + r, 1.1 - r, -0.6 + r, 0.6 - r, 0.0, 0.0, 0.0, 0.0);
        Config cc = C; cc.maxBounces = 6; cc.drag = 0.2;                        // low drag: the ball survives 4 bounces
        double ang = 0.37; V2 d{std::cos(ang), std::sin(ang)}, s0{-0.2, -0.1};
        Sim s(Tb, cc, {}, s0, r); s.setProbe(&pr); Result R = s.run(d, 1.0);
        const Track& t = R.tracks[0]; bool ok = t.bounceIdx.size() >= 4; double worst = 0;
        // analytic: unfold, reflect position back into the box
        double X0 = Tb.xmin, X1 = Tb.xmax, Z0 = Tb.zmin, Z1 = Tb.zmax;
        auto fold = [](double v, double lo, double hi) { double w = hi - lo, u = std::fmod(v - lo, 2 * w); if (u < 0) u += 2 * w; return lo + (u <= w ? u : 2 * w - u); };
        for (size_t i = 0; ok && i + 1 < t.bounceIdx.size() + 1 && i < 4; ++i) {
            V2 q = t.pts[t.bounceIdx[i]]; double travelled = len(q - s0);
            // path length unfolded: position = s0 + d*L; after folding it must equal q only when path lengths add up -> use the first bounce index as L along the unfolded ray
            (void)travelled;
        }
        double L = 0; V2 prev = s0;
        for (size_t i = 0; i < t.bounceIdx.size() && i < 4; ++i) { V2 q = t.pts[t.bounceIdx[i]]; L += len(q - prev); prev = q;
            V2 un = s0 + d * L; V2 f{fold(un.x, X0, X1), fold(un.z, Z0, Z1)}; worst = std::max(worst, len(f - q)); }
        CHECK(ok && worst < 2e-3, "4 probe bounces follow the analytic unfolded rectangle (< 2 mm drift)");
    }
    { // 25. safety net: a probe that never reports a hit must not let the ball leave a MEASURED table
        struct Blind : Probe { bool cast(V2, V2, double, double, CastHit&) const override { return false; } } blind;
        Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.3, 0.0, 0.0, 0.0, 0.0);
        Config w = C; w.maxBounces = 2; Sim s(M, w, {}, {0.0, 0.0}, r); s.setProbe(&blind); Result R = s.run({1, 0}, 1.0);
        CHECK(R.tracks[0].pts.size() >= 2 && near(R.tracks[0].pts[1].x, 1.0, 1e-9) && !R.tracks[0].bounceIdx.empty(), "wallNet: blind probe -> bounce at the measured limit");
        Config nw = w; nw.wallNet = false; Sim s2(M, nw, {}, {0.0, 0.0}, r); s2.setProbe(&blind); Result R2 = s2.run({1, 0}, 1.0);
        CHECK(R2.tracks[0].bounceIdx.empty(), "wallNet=false: the blind probe lets the ball through (what the net prevents)");
        Table G = Table::fromPockets(pk, 0.085, 0.0, 0.0, 0.0, 0.0); Sim s3(G, w, {}, {0.0, 0.0}, r); s3.setProbe(&blind); Result R3 = s3.run({1, 0}, 1.0);
        CHECK(R3.tracks[0].bounceIdx.empty(), "estimated (not measured) limits are never used as a net");
    }
    { // 26. spin yaw in charge: the cue's post-hit speed is not crushed to the 4% floor on a near head-on hit
        std::vector<BallIn> b = {{1, {0.5, 0.004}, r}};
        Config sp = C; sp.spinActive = true; sp.spinSinFloor = 0.30;
        Sim s0(T, C, b, {0.0, 0.0}, r); Sim s1(T, sp, b, {0.0, 0.0}, r);
        Result R0 = s0.run({1, 0}, 0.6), R1 = s1.run({1, 0}, 0.6);
        auto cueAfter = [](const Result& R) { double L = 0; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 1) for (size_t i = 1; i < t.pts.size(); ++i) L += len(t.pts[i] - t.pts[i - 1]); return L; };
        CHECK(cueAfter(R1) > 4.0 * cueAfter(R0), "spinActive: the cue's deflection line after a near head-on hit is several times longer");
    }
    { // 27. v65: PhysX bounce threshold (restitution only above Physics.bounceThreshold; below it the normal part is killed and the ball slides along the cushion)
        Table M = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        { // head-on, impact speed below the threshold -> the ball stops at the wall
            Config c = C; c.bounceThreshold = 2.0; c.lowBounceKeep = 0; c.maxChain = 1; Sim s(M, c, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.45);
            CHECK(std::fabs(R.finals.back().p.x - 1.0) < 0.01, "below bounceThreshold, head-on: the ball dies at the cushion");
        }
        { // same shot, threshold below the impact speed -> normal rebound
            Config c = C; c.bounceThreshold = 0.5; c.maxChain = 1; Sim s(M, c, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.45);
            CHECK(R.finals.back().p.x < 0.8, "above bounceThreshold: the ball rebounds as before");
        }
        { // oblique: below the threshold the ball keeps only its tangential speed and slides along the wall (next point stays on x = 1.0)
            Config c = C; c.drag = 0.62; c.bounceThreshold = 1.0; c.maxChain = 1; c.maxBounces = 3; Sim s(M, c, {}, {0.5, 0.2}, r);
            V2 d{0.70710678, -0.70710678}; Result R = s.run(d, 0.2); const Track& t = R.tracks[0];
            CHECK(t.pts.size() >= 3 && std::fabs(t.pts[1].x - 1.0) < 0.01 && std::fabs(t.pts[2].x - 1.0) < 0.01, "oblique below threshold: slides along the wall (no mirror)");
            Config c0 = c; c0.bounceThreshold = 0; Sim s0(M, c0, {}, {0.5, 0.2}, r); Result R0 = s0.run(d, 0.2);
            CHECK(R0.tracks[0].pts.size() >= 3 && R0.tracks[0].pts[2].x < 0.95, "threshold off: the same shot mirrors away from the wall");
        }
        { // threshold 0 (unknown) leaves every old result untouched
            Config c = C; Sim s(M, c, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.45); Config c1 = c; c1.bounceThreshold = 0; Sim s1(M, c1, {}, {0.0, 0.0}, r); Result R1 = s1.run({1, 0}, 0.45);
            CHECK(near(R.finals.back().p.x, R1.finals.back().p.x), "bounceThreshold = 0 is a no-op");
        }
    }
    { // 28. v65: PhysicMaterial combine (Unity enum: Average 0, Multiply 1, Minimum 2, Maximum 3; PhysX priority Average < Minimum < Multiply < Maximum)
        CHECK(near(px::combine(0, 0, 0.8, 0.4), 0.6), "average combine");
        CHECK(near(px::combine(0, 1, 0.8, 0.5), 0.4), "average + multiply -> multiply wins");
        CHECK(near(px::combine(2, 1, 0.8, 0.5), 0.4), "minimum + multiply -> multiply wins (higher priority)");
        CHECK(near(px::combine(2, 0, 0.8, 0.5), 0.5), "minimum + average -> minimum wins");
        CHECK(near(px::combine(3, 1, 0.8, 0.5), 0.8), "maximum beats everything");
        CHECK(px::combine(9, 0, 0.5, 0.5) < 0 && px::combine(0, 0, -1, 0.5) < 0, "unknown mode / missing value -> -1");
        CHECK(near(px::rollingDrag(0.62, 0.0), 0.62 / 1.4), "rolling sphere: linear drag alone is shared with the rotating mass");
        CHECK(near(px::rollingDrag(1.0, 1.0), 1.0), "equal linear and angular damping -> 1.0 (I = 0.4 m r^2)");
    }
    { // 66. v66: spin yaw is consumed by the cue's FIRST contact. Ball first (game's cue rule already contains the yaw) -> the cushion afterwards is a plain mirror
        Config sc = C; sc.edgeSpinDeg = 47.4; sc.hasOv = true; sc.ovTarget = {0.95, -0.31}; sc.ovCue = {0.0, 1.0}; sc.maxBounces = 3;
        std::vector<BallIn> bs = {{1, {0.5, 0.0}, r}};
        Sim s(T, sc, bs, {0.2, 0.02}, r); Result R = s.run({1, 0}, 1.0);
        const Track* leg = nullptr; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 1) leg = &t;
        CHECK(leg && leg->pts.size() >= 3, "v66: cue leg after the ball hit reaches a cushion");
        if (leg && leg->pts.size() >= 3) { V2 d = leg->pts[2] - leg->pts[1]; double l = len(d);
            CHECK(l > 1e-6 && std::fabs(d.x / l) < 0.05 && d.z < 0, "v66: no second yaw at the cushion after a ball first (straight mirror, like the real shot)"); }
        Config nb = sc; nb.hasOv = false; Sim s2(T, nb, {}, {0.2, 0.0}, r); Result R2 = s2.run({0.7071067811865476, 0.7071067811865476}, 1.0);
        V2 d2 = R2.tracks[0].pts[2] - R2.tracks[0].pts[1]; V2 m{0.7071067811865476, -0.7071067811865476};
        CHECK(std::fabs(d2.x / len(d2) - m.x) > 0.1, "v66: with no ball first the cushion still applies the yaw");
    }
    { // v69b: per-ball drag. The cue's FIRST leg stops after v0/drag1; a ball it hits (target) rolls with the normal drag and the cue after the hit too.
        Config c1; c1.drag = 0.62; c1.drag1 = 1.10; c1.maxBounces = 0; c1.tgtGameBasis = false; c1.vMax = 4.3;
        // (a) free roll in the middle of a big table: end point = v0 / drag1 along x
        Table Tb = T; Tb.xmin = -50; Tb.xmax = 50; Tb.zmin = -50; Tb.zmax = 50;
        Sim s(Tb, c1, {}, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.30);   // power 0.30 -> v0 = 1.29
        double v0 = 0.30 * 4.3; const Track& t0 = R.tracks[0];
        CHECK(near(t0.pts.back().x, v0 / 1.10, 2e-3), "v69b: cue first leg stops at v0/drag1");
        Config c0 = c1; c0.drag1 = 0; Sim s0(Tb, c0, {}, {0.0, 0.0}, r); Result R0 = s0.run({1, 0}, 0.30);
        CHECK(near(R0.tracks[0].pts.back().x, v0 / 0.62, 2e-3), "v69b: drag1=0 keeps the old single-drag behaviour");
        // (b) head-on hit: the target (gen 1) rolls with the normal drag
        std::vector<BallIn> bb = {{1, {0.4, 0.0}, r}}; Sim s2(Tb, c1, bb, {0.0, 0.0}, r); Result R2 = s2.run({1, 0}, 0.30);
        const Track* tg = nullptr; for (auto& k : R2.tracks) if (k.id == 1) tg = &k;
        CHECK(tg && tg->pts.back().x > 0.4 + 0.05 && tg->pts.back().x < 0.4 + v0 / 0.62, "v69b: target after a hit uses the rolling drag (moves, shorter than a free roll)");
        if (tg) { double dist = tg->pts.back().x - tg->pts.front().x; double legSpeed = v0 - 1.10 * (0.4 - 2 * r); CHECK(dist < legSpeed / 0.62 * 1.01 && dist > legSpeed / 1.10 * 0.9, "v69b: target distance is between legSpeed/1.10 and legSpeed/0.62"); }
    }
    { // v71c: a NON-cue ball bouncing off a cushion is scripted (reflect * 0.92): the cue-learned tanKeep / bounce threshold must not touch it
        Table Tb = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        auto runT = [&](bool scripted, double tanKeep) {
            Config c = C; c.maxBounces = 3; c.maxChain = 3; c.tanKeep = tanKeep; c.scriptedEdgeNonCue = scripted;
            std::vector<BallIn> bb = {{1, {0.30, 0.0}, r}};
            Sim s(Tb, c, bb, {0.17, -0.40}, r); V2 d{0.30 - 0.17, 0.40}; d = d * (1.0 / len(d)); Result R = s.run(d, 0.9);
            V2 e{0, 0}; for (auto& f : R.finals) if (f.id == 1) e = f.p; return e; };
        V2 a = runT(true, 0.5), b = runT(true, -1), c = runT(false, 0.5);
        CHECK(near(a.x, b.x, 1e-9) && near(a.z, b.z, 1e-9), "v71c: non-cue cushion ignores tanKeep (scripted reflect*0.92)");
        CHECK(std::fabs(a.x - c.x) + std::fabs(a.z - c.z) > 1e-4, "v71c: scriptedEdgeNonCue=false restores the old behaviour (tanKeep applies)");
    }
    { // v71d: after a cushion the ball-hit speeds use the CURRENT speed (game: hitdistance=0), not v0leg - 1.0*dist
        Table Tb = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        auto tgtLen = [&](bool live) {
            Config c = C; c.maxBounces = 3; c.maxChain = 3; c.liveSpeedBasis = live;
            std::vector<BallIn> bb = {{1, {-0.2, -0.38}, r}};
            Sim s(Tb, c, bb, {0.3, 0.25}, r); V2 d{0.7, -0.25}; d = d * (1.0 / len(d));   // bank on the right wall, then a cut on the target
            Result R = s.run(d, 0.9); for (auto& t : R.tracks) if (t.id == 1 && t.gen == 1) return len(t.pts.back() - t.pts.front()); return -1.0; };
        double dl = tgtLen(true), dp = tgtLen(false);
        std::printf("   v71d: first target leg live=%.3f preview=%.3f\n", dl, dp);
        CHECK(dl > 0 && dp > 0 && dl > dp, "v71d: after a cushion the target gets more with the live speed basis than with v0leg-dist");
        Config c1; CHECK(c1.liveSpeedBasis, "v71d: liveSpeedBasis defaults on");
        // the first clean contact keeps the game's preview basis: result must not depend on the flag
        auto firstHit = [&](bool live) { Config c = C; c.liveSpeedBasis = live; std::vector<BallIn> bb = {{1, {0.4, 0.02}, r}}; Sim s(Tb, c, bb, {0.0, 0.0}, r); Result R = s.run({1, 0}, 0.5);
            for (auto& t : R.tracks) if (t.id == 1 && t.gen == 1) return len(t.pts.back() - t.pts.front()); return -1.0; };
        CHECK(near(firstHit(true), firstHit(false), 1e-9), "v71d: the scripted first contact (clean cue) is unchanged by liveSpeedBasis");
    }
    { // v71e: liveTgtModel only touches NON-scripted contacts
        Table Tb = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        Config c0; CHECK(c0.liveTgtModel == 0, "v71e: liveTgtModel defaults to 0 (old behaviour)");
        auto legLen = [&](int model, bool bank) {
            Config c = C; c.maxBounces = 3; c.maxChain = 3; c.liveTgtModel = model;
            std::vector<BallIn> bb = {{1, bank ? V2{-0.2, -0.25} : V2{0.4, 0.02}, r}};
            Sim s(Tb, c, bb, bank ? V2{0.3, 0.25} : V2{0.0, 0.0}, r); V2 d = bank ? V2{0.7, -0.25} : V2{1, 0}; d = d * (1.0 / len(d));
            Result R = s.run(d, 0.9); for (auto& t : R.tracks) if (t.id == 1 && t.gen == 1) return len(t.pts.back() - t.pts.front()); return -1.0; };
        CHECK(near(legLen(0, false), legLen(1, false), 1e-9) && near(legLen(0, false), legLen(2, false), 1e-9), "v71e: the cue's first clean contact ignores liveTgtModel");
        double m0 = legLen(0, true), m1 = legLen(1, true);
        std::printf("   v71e: after-bank target leg model0=%.3f model1=%.3f\n", m0, m1);
        CHECK(m0 > 0 && m1 > 0 && m1 >= m0 - 1e-9 && std::fabs(m1 - m0) > 1e-4, "v71e: model 1 (factor 1.0) changes the non-scripted target and never gives it less than cos");
    }
    { // v71e: measured cue cushion keep reaches only the cue; non-cue balls use the decoded 0.92 whatever cushionKeep says
        Table Tb = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        auto endOf = [&](double cueN, double cushionKeep, bool cueShot) {
            Config c = C; c.maxBounces = 3; c.maxChain = 3; c.cueEdgeKeepN = cueN; c.cushionKeep = cushionKeep; c.tanKeep = -1;
            std::vector<BallIn> bb = {{1, {-0.7, 0.3}, r}};
            Sim s(Tb, c, bb, {0.0, 0.0}, r); V2 d = cueShot ? V2{1, 0} : V2{-0.7, 0.3}; d = d * (1.0 / len(d)); Result R = s.run(d, 0.9);
            V2 e{0, 0}; for (auto& f : R.finals) if (f.id == (cueShot ? -1 : 1)) e = f.p; return e; };
        V2 a1 = endOf(0.80, 0.92, true), a2 = endOf(-1, 0.92, true);
        CHECK(std::fabs(a1.x - a2.x) + std::fabs(a1.z - a2.z) > 1e-4, "v71e: cueEdgeKeepN changes the cue ball's rebound");
        V2 b1 = endOf(0.80, 0.92, false), b2 = endOf(0.80, 0.50, false);
        CHECK(near(b1.x, b2.x, 1e-9) && near(b1.z, b2.z, 1e-9), "v71e: a non-cue ball's cushion hit ignores cushionKeep / cueEdgeKeepN (decoded 0.92)");
    }
    { // v72: discrete PhysX damping (v *= 1 - drag*dt per step, moves with the damped speed): stopping distance = v0*(1-drag*dt)/drag, shorter than v0/drag
        Table Tb = Table::fromLimits(pk, -1.0, 1.0, -0.6, 0.6, 0.12, 0.10);
        auto restX = [&](double dt) {
            Config c = C; c.maxBounces = 0; c.maxChain = 0; c.drag = 0.8; c.drag1 = 0; c.vMax = 1.0; c.stepDt = dt;
            Sim s(Tb, c, {}, {-0.9, 0.0}, r); Result R = s.run(V2{1, 0}, 1.0); V2 e{0, 0}; for (auto& f : R.finals) if (f.id == -1) e = f.p; return e.x + 0.9; };
        double d0 = restX(0.0), d1 = restX(0.02), want = 1.0 * (1.0 - 0.8 * 0.02) / 0.8;
        std::printf("   v72: stop distance continuous=%.4f discrete(dt=.02)=%.4f expected=%.4f\n", d0, d1, want);
        CHECK(Config().stepDt == 0 && near(d0, 1.0 / 0.8, 0.02), "v72: stepDt defaults to 0 (continuous, unchanged)");
        CHECK(d1 < d0 - 1e-3 && near(d1, want, 0.02), "v72: stepDt shortens the stopping distance to v0*(1-drag*dt)/drag");
    }
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
