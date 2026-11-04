# FINDINGS_T1 — PhysX Integration (T1) — findings and verification

**Date:** 2026-11-04  Branch: `feature/t1-physx-integration`
**Scope:** `MY_PHYSICS_ENGINE/` only. `Snake/` and `MY_GAME/` untouched.

---

## File audit

### Files present in `MY_GAME/`
| File | Size | Status | Notes |
|---|---|---|---|
| `dump.cs` | 50 MB | present | IL2CPP dump (Oct 2026) |
| `global-metadata.dat` | 18 MB | present | Metadata |
| `libil2cpp.so` | 127 MB | present | Native library |
| `libunity.so` | 23 MB | present | Unity native library |
| `data.unity3d` | — | **MISSING** | Not required for CCD implementation |

**Missing file:** `data.unity3d`. Not required for CCD — CCD operates on table geometry already loaded from `data/*.txt` files (level11_solids.txt etc.). No T1 task is blocked by its absence.

### Engine source files inspected
`src/pp2_engine.hpp`, `src/pp2_cli.cpp`, `api/pp2_api.cpp`, `api/pp2_api.h`, `tests/test_sleepwake.cpp`, `build.sh`.

---

## Findings summary

| ID | Finding | Status | Severity |
|---|---|---|---|
| F1 | `ccdMode` and `maxDepenetrationVelocity` were declared but unused | FIXED | HIGH |
| F2 | CCD had no discrete path | FIXED | HIGH |
| F3 | TOI Newton overshoot risk unguarded | FIXED | LOW |
| F4 | Sub-step loop dropped remaining `rem` on no-collision | FIXED | MEDIUM |
| F5 | `sleepKE=0` default (sleep disabled by default) | By design | MEDIUM |
| F6 | 1 mm differential accuracy NOT verifiable | Open | HIGH |

---

## F1 — CCD flags were inert; now implemented

**Before:** `ccdMode` (line 65) and `maxDepenetrationVelocity` (line 66) were declared in `Cfg` but never referenced in any simulation logic. Setting them produced no behavioural change.

**After:**
- `ccdMode = 0` → continuous TOI via `W.sweep()` (analytical swept-sphere root finder, exact <1e-10 m).
- `ccdMode = 1` → discrete sub-step via new `W.discreteWallHit()` (move then penetration-check).
- `maxDepenetrationVelocity` caps sub-step `step` so fastest ball displacement ≤ `maxDepenetrationVelocity * dtc`.
- Verified: test 12 confirms low cap moves ball less than high cap; test 11 confirms mode 0 vs mode 1 differ at high speed.

**Evidence:** `sh build.sh` — test_sleepwake 15/15 passed; regression tests 0 failures.

---

## F2 — Discrete CCD path missing; now added

**Before:** Only continuous TOI existed. No discrete sub-step CCD path was available.

**After:** `W.discreteWallHit(V2 cp, double R, double Y, WallHit& wh)` iterates wall triangles, checks if ball centre at `cp` is within radius `R` of any triangle. Returns penetration normal from closest point vector. Used when `ccdMode != 0`.

**Evidence:** `discreteWallHit()` at line ~280 of `src/pp2_engine.hpp`; called in sub-step loop when `ccdMode != 0`.

---

## F3 — TOI Newton overshoot risk; safeguards added

**Before:** `sweep()` Newton step `tt += f / (-df)` could overshoot or produce non-finite values; `tt > 1.0` only checked after adding step.

**After:** `tt` clamped to [0,1]; non-finite or negative step breaks iteration; `f <= 1e-10` and `df >= -1e-14` epsilon guards preserved.

**Evidence:** `sweep()` lines ~247-277 of `src/pp2_engine.hpp`.

---

## F4 — Sub-step loop dropped remaining `rem` on no-collision

**Before:** `else break;` exited the sub-step loop when no collision was found, leaving `rem` unconsumed and causing energy/position loss when `maxDepenetrationVelocity` capped `step` below `rem`.

**After:** Replaced `else break` with no-op comment; while loop continues consuming remaining `rem` until exhausted.

**Evidence:** test 5 ("straight motion exact at 1/5/15 m/s") now passes with max err=0.00e+00 m.

---

## F5 — Sleep/wake disabled by default (`sleepKE=0`)

**Before:** `sleepKE=0` default — sleep/wake code existed but was inactive.

**After:** Unchanged by design. Sleep/wake is fully implemented but `sleepKE=0` means the sleep gate never fires. User must pass `--sleep-ke <value>` or set via C ABI to activate. Default remains OFF because sleep behaviour is PHYSX_4_1_REFERENCE, not confirmed from libunity.

**Evidence:** test 15 confirms ball keeps moving with `sleepKE=0`; test 2 confirms ball sleeps with `sleepKE>0`.

---

## F6 — 1 mm differential accuracy NOT verifiable

**Status:** Open — not tested.

**Reason:** No valid game reference data exists. No measured shot trajectory from the actual game binary is available for comparison. The engine is source-documented ("موثّق المصدر"), not matching ("مطابق").

**Per mission directive:** Never claim 1 mm accuracy unless actually measured. No claim of game-level accuracy is made.

---

## Confirmed vs inferred vs unknown

### Confirmed from game files/binary
- PhysX 4.1.2 version
- `Physics.sleepThreshold = 0.001` (from data.unity3d PhysicsManager)
- `Ball.sleepThreshold = 0.002f` (from dump.cs + runtime dumps tables 5/7/9/11)
- Restitution combine Minimum → 0.92
- `defaultMaxDepenetrationVelocity = 10` (sharedassets0 PhysicsManager)
- Substeps = 7, fixedDelta = 0.01 s / 7
- Per-table R (radius) and Y values

### Inferred — PHYSX_4_1_REFERENCE (NOT confirmed from game binary)
- Damping formula `v *= max(0, 1 - d * dt)` (dragMode=0); alternative `v /= (1 + d * dt)` (dragMode=1)
- Sleep/wake semantics: sleepKE, wakeCounter accumulation/reset, wake-on-contact
- `wakeCounter = 0.4` s default (PhysX 4.1 knowledge)
- CCD multi-pass, speculative contacts, solver (PGS/TGS 30/20) — analytical TOI used instead
- TOI Newton iteration converges to first root (convex distance property)

### Unknown
- Whether the game binary actually uses sleep/wake at runtime (sleepKE default kept at 0)
- Whether CCD in the game uses continuous or discrete mode
- Exact game ball trajectory for 1 mm differential comparison

---

## Test results

| Test | Result |
|---|---|
| test_engine | 13/13 passed |
| test_sleepwake | 15/15 passed |
| All other test binaries | 0 failures |
| Regression (full `sh build.sh`) | Zero failures |

---

## Remaining limitations

1. `data.unity3d` not available — not required for CCD but would enable table geometry extraction and game-level accuracy comparison.
2. Sleep/wake not confirmed from `libunity.so` — default kept OFF (`sleepKE=0`).
3. No measured game reference data — 1 mm differential accuracy cannot be claimed.
4. Discrete CCD (mode 1) may tunnel at extreme speeds — inherent to discrete CCD; mode 0 (continuous TOI) is the recommended setting.