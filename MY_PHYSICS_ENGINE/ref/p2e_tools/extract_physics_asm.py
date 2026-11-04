#!/usr/bin/env python3
"""
Disassemble the physics functions of Pool Live Tour from libil2cpp.so,
using the exact function boundaries taken from dump.cs.

Usage:
  python3 extract_physics_asm.py dump.cs libil2cpp.so [out.txt]
  python3 extract_physics_asm.py dump.cs --ranges          # only print ranges, no objdump

Needs llvm-objdump (or aarch64-linux-gnu-objdump) in PATH for the real run.
"""
import bisect, re, shutil, subprocess, sys

WANTED = [
    "PhysicsSolver.BallToBallCollisionDir", "PhysicsSolver.ud",
    "PhysicsSolver.BallToEdgeCollisionDir", "PhysicsSolver..cctor",
    "PhysicsBuilder.SolveBallWithCushions", "PhysicsBuilder.SolveBallWithBall",
    "PhysicsBuilder.SolveCollisions", "PhysicsBuilder.SolveWithCueBall",
    "PhysicsBuilder.Update", "PhysicsBuilder.OnFetchTableCaushions",
    "PhysicsBuilder.OnAssignPhysicsSettings", "PhysicsBuilder.UpdateLevelPower",
    "PhysicsBuilder.PredictCueDirAfterEdgeCollision",
    "PhysicsBuilder.PredictCueDirAfterBallCollision",
    "PhysicsBuilder.PredictTargetBallAfterCollision",
    "PhysicsBuilder.PredictedFinalVelocity",
    "PhysicsBuilder.PredictTargetBallFinalVelocity",
    "PhysicsBuilder.GetForceDirectionToHit",
    "HandlePhysicsCollisions.SetTargetBallAfterCollision",
    "HandlePhysicsCollisions.SetCueDirectionAfterBallCollide",
    "Ball.RollBall", "Ball.FixedUpdate", "Ball.OnCollisionEnter",
    "CueBall.SyncSpin", "CueBall.OnCastBall", "CueBall.TestForce",
    "CueBall.PredictCueRotationAngelAfterCollision", "CueBall.OnCollisionEnter",
    "CueBallView.OnCastEdge", "CueBallView.OnCastBall", "CueBallView.LateUpdate",
]

CLS = re.compile(r"^(?:public|internal|private|protected)?[\w ]*?\b(?:class|struct)\s+([\w.]+)")
RVA = re.compile(r"//\s*RVA:\s*0x([0-9A-Fa-f]+)")
METH = re.compile(r"(\S+)\(.*\)\s*\{")


def parse(path):
    funcs, all_rva, cur, pending = {}, set(), None, None
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            s = line.strip()
            m = CLS.match(line)
            if m and not line.startswith("\t"):
                cur = m.group(1).split(".")[0] if "PhysicsBuilder." not in m.group(1) else m.group(1)
                continue
            m = RVA.search(s)
            if m:
                pending = int(m.group(1), 16)
                all_rva.add(pending)
                continue
            if pending is not None and cur:
                m = METH.search(s)
                if m:
                    funcs.setdefault(f"{cur}.{m.group(1)}", pending)
                    pending = None
    return funcs, sorted(all_rva)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    funcs, rvas = parse(sys.argv[1])
    only_ranges = sys.argv[2] == "--ranges"
    out = open(sys.argv[3], "w") if len(sys.argv) > 3 else sys.stdout
    tool = shutil.which("llvm-objdump") or shutil.which("aarch64-linux-gnu-objdump") or shutil.which("objdump")
    for name in WANTED:
        start = funcs.get(name)
        if start is None:
            print(f"# MISSING in dump.cs: {name}", file=out)
            continue
        i = bisect.bisect_right(rvas, start)
        end = rvas[i] if i < len(rvas) else start + 0x400
        print(f"\n##### {name}  0x{start:X}..0x{end:X}  ({end - start} bytes)", file=out)
        if only_ranges:
            continue
        r = subprocess.run([tool, "-d", f"--start-address=0x{start:X}", f"--stop-address=0x{end:X}", sys.argv[2]],
                           capture_output=True, text=True)
        print(r.stdout if r.returncode == 0 else r.stderr, file=out)


if __name__ == "__main__":
    main()
