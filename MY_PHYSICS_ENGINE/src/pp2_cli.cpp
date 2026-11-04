// CLI: scene on stdin (or --scene file).  Lines:
//   ball <id> <x> <z> <vx> <vz>            ball with initial velocity (no game first-contact logic)
//   ball <id> <x> <z>                      ball at rest
//   thetahold <startDeg> <frames> [dt]   theta SmoothDamp not converged: start angle, frame count, Time.deltaTime (default converged)
//   spinangle <deg>   SpinController.SpinAngelDeg for the next shot (default 0 = no spin)
//   tip <h> <v> [radius] [cueDividor]  spin tip position (UI units; radius default = 60*cueDividor; |h|,|v|<=1 = reset; P2N) -> SpinFactor/SpinAngelDeg via the decoded SpinController (applied to the next shot; P2L)
//   shot <cueId> <dirx> <dirz> <F> [spin]  the game's shot: cue gets dir*F, first contact follows the game's server rules
// Output: predict / event / p (path every 0.01 s) / final / stats
#include "pp2_spinwire.hpp"
using namespace pp2;
int main(int argc, char** argv) {
    Sim S; std::string dir = "data", table; const char* scn = nullptr; bool path = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data") dir = argv[++i]; else if (a == "--table") table = argv[++i]; else if (a == "--scene") scn = argv[++i];
        else if (a == "--edge-e") S.C.eEdge = atof(argv[++i]); else if (a == "--pocket-e") S.C.ePocket = atof(argv[++i]);
        else if (a == "--ball-e") S.C.eBall = atof(argv[++i]); else if (a == "--drag-mode") S.C.dragMode = atoi(argv[++i]);
        else if (a == "--no-path") path = false; else if (a == "--tmax") S.C.tmax = atof(argv[++i]);
        else if (a == "--radius") S.C.R = atof(argv[++i]); else if (a == "--y") S.C.Y = atof(argv[++i]);
        else if (a == "--drag") S.C.drag = atof(argv[++i]); else if (a == "--no-first-contact") S.C.firstContact = false;
        else if (a == "--sleep-ke") S.C.sleepKE = atof(argv[++i]); else if (a == "--stop") S.C.stop = atof(argv[++i]); else if (a == "--ball-stop") S.C.ballStop = atof(argv[++i]);
        else if (a == "--substeps") S.C.setSubsteps((float)atof(argv[++i])); else if (a == "--no-special") S.C.special = false; else if (a == "--pocket-aabb") S.C.pocketHull = false; else if (a == "--pocket-center") S.C.pocketSphere = false; else if (a == "--pocket-prism") S.C.pocketHull3d = false;
        else if (a == "--sleep-time") S.C.wakeCounter = S.C.sleepTime = atof(argv[++i]); else if (a == "--wake-counter") S.C.wakeCounter = atof(argv[++i]); else if (a == "--ccd") S.C.ccdMode = atoi(argv[++i]); else if (a == "--max-depenetration") S.C.maxDepenetrationVelocity = atof(argv[++i]); else if (a == "--angular-drag") S.C.angularDrag = atof(argv[++i]);
    }
    // --table lvl1_4|lvl5_6|lvl7_8|lvl9|level11|lvl13 (default level11); --radius/--y after it override the preset
    bool okl = table.empty() ? S.load(dir) : S.loadTable(dir, table);
    if (!table.empty() && okl) { for (int i = 1; i < argc; ++i) { std::string a = argv[i]; if (a == "--radius") S.C.R = atof(argv[++i]); else if (a == "--y") S.C.Y = atof(argv[++i]); } if (!S.table->ySourced) fprintf(stderr, "warning: table %s Y=%.4f is ASSUMED (see docs/FINDINGS_P2W.md)\n", S.table->key, S.C.Y); }
    if (!okl) { fprintf(stderr, "cannot load data from %s\n", dir.c_str()); return 2; }
    FILE* in = scn ? fopen(scn, "r") : stdin; if (!in) { fprintf(stderr, "no scene\n"); return 2; }
    std::vector<Ball> B; Shot sh; bool haveShot = false; spinwire::Tip tip; bool haveTip = false; char line[256]; int id; double x, z, vx, vz, F, sp;
    while (fgets(line, 256, in)) {
        if (sscanf(line, "ball %d %lf %lf %lf %lf", &id, &x, &z, &vx, &vz) == 5) B.push_back({id, x, z, vx, vz});
        else if (sscanf(line, "ball %d %lf %lf", &id, &x, &z) == 3) B.push_back({id, x, z, 0, 0});
        else if (!strncmp(line, "tip ", 4)) { double rr = -1, dv = 0.75; int n2 = sscanf(line + 4, "%lf %lf %lf %lf", &tip.h, &tip.v, &rr, &dv); if (n2 >= 2) { haveTip = true; if (n2 >= 3) tip.radius = rr; if (n2 >= 4) tip.cueDividor = dv; } }
        else if (!strncmp(line, "thetahold ", 10)) { double a, d = sh.thetaDt; int f; int n3 = sscanf(line + 10, "%lf %d %lf", &a, &f, &d); if (n3 >= 2) { sh.thetaStartDeg = a; sh.thetaFrames = f; sh.thetaDt = d; } }
        else if (!strncmp(line, "spinangle ", 10)) { sscanf(line + 10, "%lf", &sh.spinAngleDeg); }
        else { int n = sscanf(line, "shot %d %lf %lf %lf %lf", &id, &x, &z, &F, &sp); if (n >= 4) { sh.cueId = id; sh.dx = x; sh.dz = z; sh.F = F; sh.spin = n == 5 ? sp : -1; haveShot = true; } }
    }
    if (scn) fclose(in);
    if (haveShot && haveTip) { if (!spinwire::applyTip(S, B, sh, tip)) fprintf(stderr, "tip: spin pipeline invalid for this shot\n"); else printf("tip spinFactor=%.5f spinAngleDeg=%.4f\n", sh.spin, sh.spinAngleDeg); }
    Result R = S.run(B, haveShot ? &sh : nullptr, path);
    if (haveShot) {
        const Prediction& p = R.pred;
        printf("predict valid=%d ball=%d target=%d hitDist=%.5f angle=%.3f(%d) theta=%.0f cueV=%.5f,%.5f targetV=%.5f,%.5f\n", p.valid, p.isBall, p.targetId, p.hitDistance, p.ballAngleDeg, p.angleInt, p.theta, p.cueV.x, p.cueV.z, p.targetV.x, p.targetV.z);
    }
    for (auto& e : R.events) printf("event %.5f ball %d %s\n", e.t, e.ball, e.what.c_str());
    if (path) for (auto& p : R.path) { printf("p %.4f", p.t); for (size_t i = 0; i < p.p.size(); ++i) printf(" %d %.6f %.6f", R.balls[i].id, p.p[i].x, p.p[i].z); printf("\n"); }
    for (auto& b : R.balls) printf("final %d %.6f %.6f %d\n", b.id, b.x, b.z, b.pocketed ? 1 : 0);
    printf("stats t=%.3f wallHits=%d ballHits=%d tris=%d flag108=%d\n", R.t, R.wallHits, R.ballHits, R.tris, R.flag108 ? 1 : 0);
    return 0;
}
