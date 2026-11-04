// PremiumPath v81: 9-ball shot suggestion (pure C++, no Android/Unity dependency).
// Geometry + the same engine (pp::Sim) as the lines - no learning, no guesses beyond the values the game gives.
//
// 9-ball rules used: the cue ball must touch the LOWEST numbered ball on the table first; after that a ball must be pocketed or a ball must reach a cushion,
// otherwise it is a foul; scratching (cue pocketed) is a foul. Ball id == ball number, 0 = cue (the payload feeds the cue separately).
//
// The DIRECT shot (cue -> lowest ball -> pocket with no cushion involved) is NOT suggested: the long aim line already shows it.
// Candidate families (all built by unfolding the table = mirror images):
//   KICK   cue ball off 1 or 2 cushions, then the lowest ball goes in (direct cut after the kick)
//   BANK   the lowest ball goes to 1 or 2 cushions and then into the pocket
//   COMBO  the lowest ball pushes another ball into a pocket
//   SAFE   no ball can be pocketed: the best legal kick that leaves the lowest ball hidden
// Every candidate is checked by the real engine (legal first contact, foul check, what drops, where the cue ball ends up), the best ones are
// re-run at other powers and with +-0.35 degrees of aim error (a shot that still drops with a small error is worth more).
#pragma once
#include "predict_core.hpp"
#include <cstdint>

namespace pp {

enum SugKind { SK_NONE = 0, SK_BANK = 1, SK_KICK = 2, SK_COMBO = 3, SK_SAFE = 4 };

struct Suggestion {
    bool ok = false;
    int kind = SK_NONE, target = -1, pocket = -1, via = -1, cushions = 0;
    V2 dir{1, 0};
    double power = 0.6, score = -1e18, angleDeg = 0;
    int potted = 0, robust = 0;          // robust = 0..2 (how many of the two +-0.35 deg runs still drop the same balls)
    bool pottedTarget = false, potted9 = false;
    V2 cueEnd{0, 0};
};

class Planner {
public:
    // balls = every ball on the table except the cue (ids = ball numbers)
    void begin(const Table& T, const Config& base, const std::vector<BallIn>& balls, V2 cue, double cueR) {
        T_ = T; C_ = base; balls_ = balls; cue_ = cue; cr_ = cueR; done_ = false; phase_ = 0; idx_ = 0; cands_.clear(); top_.clear(); best_ = Suggestion{};
        C_.hasOv = false; C_.spinActive = false; C_.edgeSpinDeg = 0; C_.spinX = C_.spinY = 0; C_.budgetMs = 0; C_.maxBounces = 8; C_.maxChain = 6;
        target_ = -1; for (auto& b : balls_) if (b.id >= 1 && (target_ < 0 || b.id < target_)) target_ = b.id;
        if (target_ < 0) { done_ = true; return; }
        for (auto& b : balls_) if (b.id == target_) { tp_ = b.p; r_ = b.r; }
        gen();
        if (cands_.empty()) done_ = true;
    }
    bool done() const { return done_; }
    int target() const { return target_; }
    int candidates() const { return (int)cands_.size(); }
    const Suggestion& best() const { return best_; }
    Result replay() const { Sim sim(T_, C_, balls_, cue_, cr_); return sim.run(best_.dir, best_.power); }

    // runs jobs until the time budget is spent; true when finished
    bool step(double budgetMs) {
        if (done_) return true;
        const auto t0 = std::chrono::steady_clock::now();
        while (!done_) {
            doOne();
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs) break;
        }
        return done_;
    }

private:
    struct Eval { bool legal = false; double score = -1e18; uint64_t mask = 0; int potted = 0; bool tPot = false, p9 = false; V2 cueEnd{0, 0}; };
    struct Cand { V2 dir; int kind; int pocket, cushions, via; double score = -1e18, power = 0.6; int robust = 0; Eval ev; };

    Table T_; Config C_; std::vector<BallIn> balls_; V2 cue_{0, 0}; double cr_ = 0.03, r_ = 0.03; int target_ = -1; V2 tp_{0, 0};
    std::vector<Cand> cands_; std::vector<int> top_; Suggestion best_;
    bool done_ = false; int phase_ = 0; size_t idx_ = 0;

    static V2 unit(V2 a) { double l = len(a); return l > 1e-12 ? a * (1.0 / l) : V2{1, 0}; }
    V2 mir(V2 p, int w) const { switch (w) { case 0: return {2 * T_.xmin - p.x, p.z}; case 1: return {2 * T_.xmax - p.x, p.z}; case 2: return {p.x, 2 * T_.zmin - p.z}; default: return {p.x, 2 * T_.zmax - p.z}; } }
    static double segDist(V2 a, V2 b, V2 p) { V2 ab = b - a; double l2 = dot(ab, ab); double t = l2 > 1e-12 ? std::max(0.0, std::min(1.0, dot(p - a, ab) / l2)) : 0.0; return len(p - (a + ab * t)); }
    static V2 rot(V2 d, double a) { double c = std::cos(a), s = std::sin(a); return {d.x * c - d.z * s, d.x * s + d.z * c}; }

    void add(V2 aim, int kind, int pocket, int cush, int via) {
        if (len(aim) < 1e-9) return; V2 d = unit(aim);
        for (auto& c : cands_) if (c.kind == kind && c.pocket == pocket && c.via == via && std::fabs(c.dir.x - d.x) + std::fabs(c.dir.z - d.z) < 3e-4) return;
        Cand c; c.dir = d; c.kind = kind; c.pocket = pocket; c.cushions = cush; c.via = via; cands_.push_back(c);
    }
    void gen() {
        const double d2 = 2.0 * r_;
        for (size_t k = 0; k < T_.pockets.size(); ++k) {
            const V2 P = T_.pockets[k].p;
            const V2 Gd = tp_ - unit(P - tp_) * d2;                                   // cue-ball centre for a straight pot of the target into this pocket
            for (int a = 0; a < 4; ++a) add(mir(Gd, a) - cue_, SK_KICK, (int)k, 1, -1);
            for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b) if (a != b) add(mir(mir(Gd, b), a) - cue_, SK_KICK, (int)k, 2, -1);
            for (int a = 0; a < 4; ++a) { V2 Pm = mir(P, a); add(tp_ - unit(Pm - tp_) * d2 - cue_, SK_BANK, (int)k, 1, -1); }
            for (int a = 0; a < 4; ++a) for (int b = 0; b < 4; ++b) if (a != b) { V2 Pm = mir(mir(P, b), a); add(tp_ - unit(Pm - tp_) * d2 - cue_, SK_BANK, (int)k, 2, -1); }
            for (auto& B : balls_) {
                if (B.id == target_ || B.id < 1) continue;
                V2 u = unit(P - B.p), GB = B.p - u * d2;                               // where the target must be when it touches B so that B leaves toward the pocket
                V2 t = unit(GB - tp_); V2 GT = tp_ - t * d2;
                add(GT - cue_, SK_COMBO, (int)k, 0, B.id);
            }
        }
    }

    Eval eval(V2 dir, double power, const Cand& c) const {
        Eval E; Sim sim(T_, C_, balls_, cue_, cr_); Result R = sim.run(dir, power);
        const Track* ct = nullptr; for (auto& t : R.tracks) if (t.id == -1 && t.gen == 0) { ct = &t; break; }
        if (!ct || ct->end != END_BALL || ct->hitId != target_ || ct->pts.size() < 2) return E;          // wrong first contact = foul
        const Final* cf = nullptr; for (auto& f : R.finals) if (f.id == -1) cf = &f;
        if (!cf || cf->pocketed) return E;                                                             // scratch
        for (auto& f : R.finals) if (f.id != -1 && f.pocketed) { ++E.potted; if (f.id < 64) E.mask |= 1ULL << f.id; if (f.id == target_) E.tPot = true; if (f.id == 9) E.p9 = true; }
        bool rail = false; for (auto& t : R.tracks) { if (t.id == -1 && t.gen == 0) continue; if (!t.bounceIdx.empty()) rail = true; }
        if (!E.potted && !rail) return E;                                                              // no ball potted and nothing reached a cushion = foul
        if (E.tPot) { bool tRail = false; for (auto& t : R.tracks) if (t.id == target_ && !t.bounceIdx.empty()) tRail = true; if (ct->bounceIdx.empty() && !tRail) return E; }   // direct pot: not suggested (the long line has it)
        const double W = std::max(0.5, T_.xmax - T_.xmin);
        double legLen = 0; for (size_t i = 1; i < ct->pts.size(); ++i) legLen += len(ct->pts[i] - ct->pts[i - 1]);
        double tLen = 0; for (auto& t : R.tracks) if (t.id == target_) for (size_t i = 1; i < t.pts.size(); ++i) tLen += len(t.pts[i] - t.pts[i - 1]);
        V2 dc = unit(ct->pts.back() - ct->pts[ct->pts.size() - 2]), n = unit(tp_ - ct->ghost); double cosCut = std::max(0.0, dot(dc, n));
        double pen = 14.0 * legLen / W + 10.0 * tLen / W + 22.0 * (1.0 - cosCut) + 5.0 * power + (c.kind == SK_BANK ? 9.0 * c.cushions : c.kind == SK_KICK ? 8.0 * c.cushions : 12.0);
        pen = std::min(pen, 60.0);
        E.cueEnd = cf->p; double s = 0;
        auto lowestLeft = [&](int& id, V2& pos) { id = -1; for (auto& f : R.finals) if (f.id >= 1 && !f.pocketed && (id < 0 || f.id < id)) { id = f.id; pos = f.p; } };
        auto visible = [&](V2 from, int toId, V2 to) { for (auto& f : R.finals) if (f.id != -1 && f.id != toId && !f.pocketed && segDist(from, to, f.p) < 2.0 * r_ * 0.95) return false; return true; };
        if (E.potted) {
            s = E.p9 ? 200.0 : E.tPot ? 100.0 : 70.0; s += 10.0 * std::min(2, E.potted - 1);
            if (!E.p9) { int nid; V2 np; lowestLeft(nid, np);
                if (nid >= 0) { s += visible(cf->p, nid, np) ? 25.0 : 0.0; double dn = len(np - cf->p) / W; s += 10.0 * std::max(0.0, 1.0 - std::fabs(dn - 0.35) / 0.35); }
                double dp = 1e9; for (auto& pk : T_.pockets) dp = std::min(dp, len(pk.p - cf->p)); if (dp < 0.12 * W) s -= 15.0;
                if (cf->p.x < T_.xmin + 3 * r_ || cf->p.x > T_.xmax - 3 * r_ || cf->p.z < T_.zmin + 3 * r_ || cf->p.z > T_.zmax - 3 * r_) s -= 8.0; }
            E.score = s - pen;
        } else {                                                                                       // safety: the lowest ball is hidden from the cue ball afterwards
            int nid; V2 np; lowestLeft(nid, np); s = -50.0; if (nid >= 0 && !visible(cf->p, nid, np)) s += 30.0;
            E.score = s - 0.3 * pen;
        }
        E.legal = true; return E;
    }

    void doOne() {
        if (phase_ == 0) {                                                                             // every candidate once at power 0.6
            if (idx_ >= cands_.size()) {
                std::vector<int> order; for (size_t i = 0; i < cands_.size(); ++i) if (cands_[i].ev.legal) order.push_back((int)i);
                std::sort(order.begin(), order.end(), [&](int a, int b) { return cands_[a].score > cands_[b].score; });
                if (order.size() > 14) order.resize(14); top_ = order; phase_ = 1; idx_ = 0;
                if (top_.empty()) done_ = true; return;
            }
            Cand& c = cands_[idx_++]; c.ev = eval(c.dir, 0.6, c); c.score = c.ev.legal ? c.ev.score : -1e18; c.power = 0.6; return;
        }
        if (phase_ == 1) {                                                                             // best power per top candidate
            static const double PW[4] = {0.3, 0.45, 0.75, 0.9};
            if (idx_ >= top_.size() * 4) { phase_ = 2; idx_ = 0; return; }
            Cand& c = cands_[top_[idx_ / 4]]; double p = PW[idx_ % 4]; ++idx_;
            Eval e = eval(c.dir, p, c); if (e.legal && e.mask == c.ev.mask && e.score > c.score) { c.score = e.score; c.power = p; c.ev = e; }
            return;
        }
        if (phase_ == 2) {                                                                             // tolerance to aim error
            if (idx_ >= top_.size() * 2) { finish(); return; }
            Cand& c = cands_[top_[idx_ / 2]]; double da = (idx_ % 2 == 0 ? -1.0 : 1.0) * 0.35 * 3.14159265358979323846 / 180.0; ++idx_;
            if (c.ev.potted) { Eval e = eval(rot(c.dir, da), c.power, c); if (e.legal && (e.mask & c.ev.mask) == c.ev.mask) ++c.robust; }
            return;
        }
    }
    void finish() {
        done_ = true; int bi = -1; double bs = -1e18;
        for (int i : top_) { Cand& c = cands_[i]; double s = c.score; if (c.ev.potted) s += c.robust == 2 ? 30.0 : c.robust == 1 ? 12.0 : -25.0; if (s > bs) { bs = s; bi = i; } }
        if (bi < 0) return; const Cand& c = cands_[bi];
        best_.ok = true; best_.kind = c.ev.potted ? c.kind : SK_SAFE; best_.target = target_; best_.pocket = c.ev.potted ? c.pocket : -1; best_.via = c.via; best_.cushions = c.cushions;
        best_.dir = c.dir; best_.power = c.power; best_.score = bs; best_.angleDeg = std::atan2(c.dir.z, c.dir.x) * 180.0 / 3.14159265358979323846;
        best_.potted = c.ev.potted; best_.robust = c.robust; best_.pottedTarget = c.ev.tPot; best_.potted9 = c.ev.p9; best_.cueEnd = c.ev.cueEnd;
    }
};


// ---------------------------------------------------------------------------------------------------------------------------------------------
// v82: BREAK suggestion (full diamond rack of 9 balls, cue ball not placed yet).
// The candidate breaks were found offline with this same engine on table 11 (cue ball ~1.2 m behind the apex ball, ~0.1 m off the rack axis,
// aimed almost full at the apex ball, 1 cm to the opposite side, full power). Here they are stored RELATIVE TO THE RACK (apex ball + rack axis), so they follow
// the live rack of any table, and every candidate (and its mirror image) is re-run with the LIVE engine and LIVE ball positions at 5 small aim errors;
// the best average wins. Nothing here depends on ball numbers: the apex is the ball nearest to the cue ball, the "9" is the ball nearest to the rack centre.
// ---------------------------------------------------------------------------------------------------------------------------------------------
struct BreakSuggestion {
    bool ok = false;
    V2 spot{0, 0}, dir{1, 0};         // where to put the cue ball, and the aim direction from there
    double power = 1.0, angleDeg = 0;
    double expPotted = 0, pScratch = 0, pNine = 0, pLegal = 0, score = -1e18;
};

class BreakPlanner {
public:
    void begin(const Table& T, const Config& base, const std::vector<BallIn>& balls, V2 cueNow, double cueR) {
        T_ = T; C_ = base; balls_ = balls; cr_ = cueR; best_ = BreakSuggestion{}; done_ = false; idx_ = 0; cands_.clear(); sum_.clear();
        C_.hasOv = false; C_.spinActive = false; C_.edgeSpinDeg = 0; C_.spinX = C_.spinY = 0; C_.budgetMs = 0; C_.maxBounces = 8; C_.maxChain = 6;
        if (balls_.size() != 9) { done_ = true; return; }
        V2 c{0, 0}; for (auto& b : balls_) c = c + b.p; c = c * (1.0 / 9.0); cen_ = c;
        int ai = 0; for (size_t i = 1; i < balls_.size(); ++i) if (len(balls_[i].p - cueNow) < len(balls_[ai].p - cueNow)) ai = (int)i;
        apexId_ = balls_[ai].id; apex_ = balls_[ai].p; r_ = balls_[ai].r;
        int ni = 0; for (size_t i = 1; i < balls_.size(); ++i) if (len(balls_[i].p - c) < len(balls_[ni].p - c)) ni = (int)i;
        nineId_ = balls_[ni].id;
        V2 f = c - apex_; double fl = len(f); if (fl < 1e-6) { done_ = true; return; }
        for (auto& b : balls_) if (len(b.p - apex_) > 12.0 * r_) { done_ = true; return; }     // not a compact rack
        f_ = f * (1.0 / fl); perp_ = {-f_.z, f_.x};
        // relative candidates: {distance behind apex, sideways offset, aim offset (opposite side), power}
        static const double REL[][4] = {{1.1807, 0.1014, -0.010, 1.0}, {1.4807, 0.1014, -0.010, 1.0}, {1.0307, 0.1014, -0.010, 1.0}, {1.0307, 0.3014, -0.018, 1.0},
                                        {1.1807, 0.1014, -0.018, 1.0}, {1.1807, 0.3014, -0.018, 1.0}, {1.1807, 0.0000, -0.010, 1.0}, {1.1807, 0.2000, -0.010, 1.0}};
        for (auto& q : REL) for (int sgn = 1; sgn >= -1; sgn -= 2) {
            if (sgn == -1 && std::fabs(q[1]) < 1e-9) continue;
            Cand k; k.spot = apex_ - f_ * q[0] + perp_ * (sgn * q[1]); k.aim = apex_ + perp_ * (sgn * q[2]); k.power = q[3];
            if (!T_.inside(k.spot, -0.01)) continue;
            V2 d = k.aim - k.spot; double l = len(d); if (l < 1e-6) continue; k.dir = d * (1.0 / l); cands_.push_back(k);
        }
        sum_.assign(cands_.size(), Acc{});
        if (cands_.empty()) done_ = true;
    }
    bool done() const { return done_; }
    int candidates() const { return (int)cands_.size(); }
    const BreakSuggestion& best() const { return best_; }
    Result replay() const { Sim sim(T_, C_, balls_, best_.spot, cr_); return sim.run(best_.dir, best_.power); }
    bool step(double budgetMs) {
        if (done_) return true;
        const auto t0 = std::chrono::steady_clock::now();
        while (!done_) {
            const size_t ci = idx_ / NJ, ji = idx_ % NJ;
            if (ci >= cands_.size()) { finish(); break; }
            evalOne(ci, JIT[ji]); ++idx_;
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs) break;
        }
        return done_;
    }
private:
    struct Cand { V2 spot, aim, dir; double power = 1.0; };
    struct Acc { double score = 0, pot = 0, scr = 0, nine = 0, legal = 0, n = 0; };
    static constexpr int NJ = 5; static constexpr double JIT[NJ] = {-0.25, -0.12, 0.0, 0.12, 0.25};
    Table T_; Config C_; std::vector<BallIn> balls_; double cr_ = 0.03, r_ = 0.03; V2 apex_{0, 0}, cen_{0, 0}, f_{1, 0}, perp_{0, 1}; int apexId_ = -1, nineId_ = -1;
    std::vector<Cand> cands_; std::vector<Acc> sum_; BreakSuggestion best_; bool done_ = false; size_t idx_ = 0;

    void evalOne(size_t ci, double jitDeg) {
        const Cand& k = cands_[ci]; Sim sim(T_, C_, balls_, k.spot, cr_);
        double a = std::atan2(k.dir.z, k.dir.x) + jitDeg * 3.14159265358979323846 / 180.0; V2 d{std::cos(a), std::sin(a)};
        Result R = sim.run(d, k.power);
        bool scr = false, p9 = false, first = false; int pot = 0;
        for (auto& f : R.finals) { if (f.id == -1) { if (f.pocketed) scr = true; } else if (f.pocketed) { ++pot; if (f.id == nineId_) p9 = true; } }
        for (auto& t : R.tracks) if (t.id == -1 && t.gen == 0) first = (t.end == END_BALL && t.hitId == apexId_);
        std::vector<int> rails; for (auto& t : R.tracks) if (t.id != -1 && !t.bounceIdx.empty() && std::find(rails.begin(), rails.end(), t.id) == rails.end()) rails.push_back(t.id);
        const bool legal = first && (pot > 0 || rails.size() >= 4);
        double sc = 0; if (scr) sc -= 3.0; if (!legal) sc -= 2.0;
        if (!scr && legal) { if (p9) sc += 8.0; sc += pot - (p9 ? 1 : 0); }
        Acc& A = sum_[ci]; A.score += sc; A.pot += scr ? 0 : pot; A.scr += scr; A.nine += (p9 && !scr && legal); A.legal += legal; A.n += 1;
    }
    void finish() {
        done_ = true; int bi = -1; double bs = -1e18;
        for (size_t i = 0; i < cands_.size(); ++i) { double s = sum_[i].n > 0 ? sum_[i].score / sum_[i].n : -1e18; if (s > bs) { bs = s; bi = (int)i; } }
        if (bi < 0) return; const Cand& k = cands_[bi]; const Acc& A = sum_[bi];
        best_.ok = true; best_.spot = k.spot; best_.dir = k.dir; best_.power = k.power; best_.angleDeg = std::atan2(k.dir.z, k.dir.x) * 180.0 / 3.14159265358979323846;
        best_.expPotted = A.pot / A.n; best_.pScratch = A.scr / A.n; best_.pNine = A.nine / A.n; best_.pLegal = A.legal / A.n; best_.score = bs;
    }
};
constexpr double BreakPlanner::JIT[BreakPlanner::NJ];

}  // namespace pp
