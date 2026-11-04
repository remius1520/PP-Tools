// PP2 core (phase 1): pp2_phys.txt - the physics constants the payload READ from the game, plus the pockets. Same text file is written by the
// payload and read by tools/replay_cli.cpp, so the offline replay uses exactly the numbers the game reported.
//   key=value            (float)
//   pocket <loc> <x> <z>  (Pocket.orbitPoint world position, loc = PocketLocation 1..6)
//   # comment
#pragma once
#include "pp2_sim.hpp"
#include <map>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
namespace pp2 { namespace sim {

struct PhysFile {
    std::map<std::string, double> kv; std::vector<std::pair<int, V2>> pockets; std::vector<std::string> notes;   // notes = free text, written as "# ..." lines
    bool has(const char* k) const { return kv.count(k) > 0; }
    double get(const char* k, double def) const { auto it = kv.find(k); return it == kv.end() ? def : it->second; }
    void set(const std::string& k, double v) { kv[k] = v; }
    static PhysFile parse(const std::string& text) {
        PhysFile f; std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (ln.empty() || ln[0] == '#') continue;
            if (ln.compare(0, 7, "pocket ") == 0) { int l; double x, z; if (sscanf(ln.c_str() + 7, "%d %lf %lf", &l, &x, &z) == 3) f.pockets.push_back({l, V2{x, z}}); continue; }
            size_t e = ln.find('='); if (e == std::string::npos) continue;
            char* end = nullptr; double v = strtod(ln.c_str() + e + 1, &end); if (end == ln.c_str() + e + 1) continue; f.kv[ln.substr(0, e)] = v;
        }
        return f;
    }
    std::string serialize() const {
        std::string o = "# PP2 physics constants read from the game (see HANDOFF_P1.md)\n"; char b[160];
        for (auto& n : notes) o += "# " + n + "\n";
        for (auto& e : kv) { snprintf(b, sizeof b, "%s=%.6g\n", e.first.c_str(), e.second); o += b; }
        for (auto& p : pockets) { snprintf(b, sizeof b, "pocket %d %.5f %.5f\n", p.first, p.second.x, p.second.z); o += b; }
        return o;
    }
    // Params from what the game reported; anything not read keeps the default of Params (documented there)
    Params toParams() const {
        Params P; P.dt = get("fixedDeltaTime", P.dt); P.substeps = (int)get("server.substeps", P.substeps); P.stopSpeed = get("server.stopThreshold", P.stopSpeed); P.drag = get("ball.linearDamping", P.drag); P.ballR = get("ball.radius", P.ballR);
        P.bounceThreshold = get("physics.bounceThreshold", P.bounceThreshold);
        // PhysX combines the two materials of a contact with the mode of the one that has the higher priority; ball-ball = the ball material twice
        if (has("ball.bounciness")) { double b = get("ball.bounciness", 0); P.ballKeep = combine((int)get("ball.bounceCombine", 0), b, b); }
        if (has("cushion.bounciness") && has("ball.bounciness")) {
            int mb = (int)get("ball.bounceCombine", 0), mc = (int)get("cushion.bounceCombine", 0); int mode = rank(mc) > rank(mb) ? mc : mb;
            P.cushionKeep = combine(mode, get("ball.bounciness", 0), get("cushion.bounciness", 0));
        }
        return P;
    }
    // UnityEngine.PhysicsMaterialCombine as numbered by the game's Unity (taken from the PP v85 notes, and logged raw in pp2_phys.txt so it can be checked):
    // 0 Average, 1 Multiply, 2 Minimum, 3 Maximum. PhysX priority: Average < Minimum < Multiply < Maximum.
    static int rank(int mode) { switch (mode) { case 0: return 0; case 2: return 1; case 1: return 2; case 3: return 3; default: return 0; } }
    static double combine(int mode, double a, double b) { switch (mode) { case 1: return a * b; case 2: return std::min(a, b); case 3: return std::max(a, b); default: return 0.5 * (a + b); } }
};
}}
