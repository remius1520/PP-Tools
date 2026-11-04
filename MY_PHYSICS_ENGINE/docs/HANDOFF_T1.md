# HANDOFF_T1 — PhysX Integration (T1) — progress and completed work

**Date:** 2026-11-04  Session branch: `feature/t1-physx-integration`
**Scope:** Work ONLY in `MY_GAME/` and `MY_PHYSICS_ENGINE/`. NEVER touch `Snake/`.
**Scope note:** `ccd_lab/` and root zips are external evidence only.

---

## Repository state

- `git status`: clean working tree on `feature/t1-physx-integration`; one commit `3ca720c Add files via upload`.
- No prior T1 handoff document existed before this session.
- Baseline: `sh build.sh` builds cleanly with g++ (host only, no NDK) and all tests pass.
- Built artifacts: `pp2_sim`, `libpp2_sim.so`, all `test_*` binaries.

---

## What was implemented in this session (T1 completed)

All work confined to `MY_PHYSICS_ENGINE/`. No T2/T3/overlay/live/APK work started.

### 1. PhysX 4.1 sleep/wake model — `src/pp2_engine.hpp`
- **KE threshold (`sleepKE`)**: per-body sleep candidate when `0.5*(vx²+vz²+aw²) < sleepKE` (per unit mass). Default = `0` (OFF) — sleep is implemented but disabled until confirmed from the game binary (P2Z3 §4: sleep behaviour is PHYSX_4_1_REFERENCE, not confirmed from libunity).
- **Per-body wakeCounter**: accumulates `dtc` while below threshold; resets to 0 when KE rises above threshold or on any contact (wake-up). Body sleeps when `wakeCounter >= wakeCounterLimit` (default 0.4 s).
- **Wake on collision**: when a sleeping body participates in a wall or ball-ball contact, `sleeping=false` and `wakeCounter=0` (PhysX 4.1: any contact wakes a body).
- Sleeping balls have velocities zeroed once `sleeping=true`.
- `Ball` struct already had `sleepT`, `wakeCounter`, `sleeping` fields — now wired into the sub-step loop.

### 2. CCD implementation — `src/pp2_engine.hpp`
- **`ccdMode = 0` (continuous TOI)**: analytical swept-sphere time-of-impact via `W.sweep()` — Newton-iteration root finder with numerical safeguards (tt clamped to [0,1], non-finite step guard, max 60 iterations). No tunnelling for this path.
- **`ccdMode = 1` (discrete sub-step)**: NEW `W.discreteWallHit()` — move ball by full step, then check penetration against wall triangles. May tunnel at extreme speeds (expected behaviour for discrete CCD).
- `ccdMode` is **read by the simulation loop** — mode 0 vs mode 1 produce measurably different results at high speed.
- `maxDepenetrationVelocity = 10.0` — from `Physics.defaultMaxDepenetrationVelocity` (PhysicsManager, GAME_FILE_FACTS: confirmed from sharedassets0). NEW: caps sub-step `step` so fastest ball's displacement <= `maxDepenetrationVelocity * dtc`. Prevents tunnelling at extreme speeds.
- Analytical TOI sweep remains the CCD-equivalent path (no real PhysX CCD binary available; marked PHYSX_4_1_REFERENCE).

### 3. TOI numerical stability fixes — `src/pp2_engine.hpp`
- `sweep()`: tt clamped to [0,1]; non-finite or negative Newton step breaks iteration; epsilon guards on `df` and `dist`.
- Sub-step loop: `else break` replaced with continue-loop so remaining `rem` is always consumed (fixes energy loss when `maxDepenetrationVelocity` caps step).

### 4. Configuration exposed via CLI and API
**CLI flags (`src/pp2_cli.cpp`):** `--sleep-ke`, `--sleep-time`, `--wake-counter`, `--ccd`, `--max-depenetration`, `--angular-drag`, `--ball-stop`, `--stop`, `--drag-mode`, `--substeps`, `--no-special`, pocket flags.

**C ABI (`api/pp2_api.cpp`, `pp2_set`):** keys `R`, `Y`, `drag`, `dragMode`, `angularDrag`, `eBall`, `eEdge`, `ePocket`, `eDefault`, `stop`, `ballStop`, `tmax`, `sleepKE`, `sleepTime`, `wakeCounter`, `ccdMode`, `maxDepenetrationVelocity`, `firstContact`.

### 5. Tests added — `tests/test_sleepwake.cpp` (15 checks, all PASS)
- test 1: sleepKE=0: ball keeps moving (never sleeps)
- test 2: sleepKE>0 with small wakeCounter: ball eventually sleeps (v=0)
- test 3: sleepKE + drag=0 + short tmax: KE stays above threshold, no sleep before tmax
- test 4: sleepKE>0 collision still transfers momentum to target
- test 5: dragMode=0 vs dragMode=1: both decay, results differ
- test 6: CCD mode0: fast ball stopped at/before cushion face (no tunnel)
- test 7: CCD mode1: same shot produces finite result
- test 8: high-speed ball-ball: momentum conserved (approx)
- test 9: 3-ball chain: no NaN, all finite
- test 10: resting contact vs cushion: no tunnel/overflow, stays on table
- test 11: zero-time touching balls: finite result
- test 12: CCD mode0 vs mode1: x differs at high speed (modes actually differ)
- test 13: maxDepenetrationVelocity: low cap moves ball less (config affects sim)
- test 14: wake-on-collision: target ball wakes and moves after hit
- test 15: sleepKE=0 default: ball keeps moving (sleep disabled)

Added to `build.sh` loop (`sleepwake` in the test list).

---

## Regression verification

`sh build.sh` — full suite, zero regressions:
- test_engine: 13/13
- test_solvers: 10/10
- test_spin: 96 checks, 0 failed
- test_postcollision: 24 checks, 0 failed
- test_diff_A: 22 checks, 0 failed
- test_clientpredict: 8 checks, 0 failed
- test_spinwire: 18 checks, 0 failed
- test_servertracker: 16 checks, 0 failed
- test_special: 5607 checks, 0 failed
- test_pockethull: 24 checks, 0 failed
- test_tables: 789 checks, 0 failed
- test_hull3d: 78 checks, 0 failed
- test_merge: 8 checks, 0 failed
- test_endshot: 8 checks, 0 failed
- test_sleepwake: 15/15 passed
- test_api: api v3, 0 fails
- test_live: 24 checks, 0 failed
- test_il2cpp_host: 12 checks, 0 failed

---

## Confirmed vs inferred (documentation)

**Confirmed from game files/binary:**
- PhysX 4.1.2 version, `Ball.sleepThreshold=0.002f`, damping chain `Rigidbody.set_linearDamping` → `Sc::BodyCore` +0x78/+0x7C, restitution combine Minimum → 0.92, `defaultMaxDepenetrationVelocity=10`, end-of-shot rule, substeps=7, fixedDelta, per-table R/Y.
- wakeCounter init=0.4f at `BodyCore+0x9c` from `Sc::BodyCore` ctor at 0x13df9a0 (`str w8,[x19,#0x9c]` with w8=0x3ecccccd).
- `updateWakeCounter` at 0x13ce6e8 exists and executes without fault; references three global constants via ADRP+LDR:
  `0x1aa000+0xd80` (=0x1aad80) = **0.2f**; `0x1ab000+0x54` (=0x1ab054) = **0.4f**; `0x1ab000+0x278` (=0x1ab278) ≈ **1e-16**.
  **None equals 0.001.** `sleepThreshold=0.001` is NOT confirmed from any available binary (data.unity3d is MISSING; the `0.001` figure comes only from data.unity3d PhysicsManager, which is not present in this workspace).
- `updateWakeCounter` compares wakeCounter vs **0.2f** (`0x1aad80`):
  - `wakeCounter >= 0.2f` → decay path `wakeCounter = max(wakeCounter - dt, 0)` — **confirmed by execution traces**: 0.4 → 0.39 → 0.38 with dt=0.01.
  - `wakeCounter < 0.2f` → alternate path (observed 0.0 → 0.4 under emulation) but exact semantics (reset vs increment) not fully pinned; object layout was approximate.
- Sleep path at 0x13ce930–0x13ce93c clears wakeCounter (+0x98/+0xa0) and linear/angular velocity (+0x90/+0xa8) — present in disassembly, not confirmed to execute at runtime due to layout approximation.

**Inferred / PHYSX_4_1_REFERENCE (NOT confirmed from game binary):**
- Damping formula `v*=max(0,1-d*dt)` (dragMode=0 default; alternative dragMode=1 exposed).
- Sleep/wake KE formula `KE = 0.5*(vx²+vz²+aw²)` with `sleepKE` threshold; the exact numeric threshold constant was NOT pinned to a binary global (no global equals 0.001). The `s3` operand of the KE comparison at `0x13ce8b8` could not be resolved to a global constant (approximate object layout returned garbage).
- wakeCounter=0.4 s default (PhysX 4.1 knowledge — matches binary init).
- Exact energy formula, increment/decay semantics, sleep threshold comparison — partially verified via emulation (decay confirmed; KE comparison not pinned).
- CCD multi-pass, speculative contacts, solver (PGS/TGS 30/20) — analytical TOI used instead.

---

## PhysX license notices

- `src/pp2_engine.hpp`: all PhysX 4.1 behavioural claims marked `[PHYSX_4_1_REFERENCE: ... NOT confirmed from game binary]`. No PhysX source code copied; only documented public reference behaviour.
- `ref/p2e_core_sim/pp2_mesh.hpp`, `pp2_physfile.hpp`, `pp2_game.hpp`: existing PhysX references preserved unchanged.
- No NVIDIA/PhysX license text exists in the repo to preserve; none was removed.

---

## Sleep/wake confirmation from libunity.so (NEW)

**`updateWakeCounter` found at `0x13ce6e8`** in `libunity.so` (Unity 6000.3.9f1, PhysX 4.1.2). Verified by:

1. Disassembly of the full function (0x13ce6e8–0x13ce99c).
2. Unicorn emulation run — function executes without fault; wakeCounter at `BodyCore+0x9c` init **0.4f** confirmed from BodyCore ctor at 0x13df9a0 (`str w8,[x19,#0x9c]` = `0x3ecccccd` = 0.4f). Unicorn object layout was approximate (field offsets guessed), so some branches could not be confirmed at runtime.

**Confirmed from the binary:**
- `Sc::BodyCore` ctor at 0x13df9a0 stores wakeCounter=0.4f at `BodyCore+0x9c` (`str w8,[x19,#0x9c]` with w8=`0x3ecccccd` = 0.4f).
- `updateWakeCounter` references global constants at `[0x1aa000+0xd80]`, `[0x1ab000+0x54]`, `[0x1ab000+0x278]` via ADRP+LDR pairs:
  `0x1aad80` = `0x3e4ccccc` = **0.2f**; `0x1ab054` = `0x3ecccccc` = **0.4f**; `0x1ab278` = `0x24e69595` ≈ **1e-16**.
- Function entry at 0x13ce6e8 executes without fault under Unicorn emulation.
- Decay path confirmed by trace: wakeCounter 0.4 → 0.39 → 0.38 with dt=0.01 (`wakeCounter = max(wakeCounter - dt, 0)` when `wakeCounter >= 0.2f`).

**NOT confirmed from the binary (still PHYSX_4_1_REFERENCE / inferred):**
- Exact energy formula (`KE = 0.5*(vx²+vz²+aw²)` with angular component) — `s3` operand of the KE comparison at `0x13ce8b8` could not be resolved to a global constant.
- wakeCounter increment/reset semantics for `wakeCounter < 0.2f` — observed under emulation but not fully pinned due to approximate object layout.
- Sleep when `wakeCounter >= wakeCounterLimit` then zeroes velocities — sleep path present in disassembly, not confirmed to execute at runtime.
- Exact numeric threshold constant value — none of the three referenced globals equals 0.001; `sleepThreshold=0.001` comes only from data.unity3d (MISSING).
- Whether the game sets `Rigidbody.sleepThreshold` (no wrapper in dump.cs ⇒ falls back to `Physics.sleepThreshold=0.001` from data.unity3d).

## Next actions (do NOT start T2/T3/overlay/live/APK)

- [DONE] Confirm `updateWakeCounter` exists at 0x13ce6e8 and executes without fault — confirmed (function found, emulation runs, wakeCounter init=0.4f confirmed from BodyCore ctor).
- [RESOLVED — UNCONFIRMED] Exact sleep threshold numeric value from global constants — the three globals referenced by `updateWakeCounter` (ADRP+LDR via `0x1aa000`/`0x1ab000`) were read directly from `libunity.so`:
    - `0x1aad80` = `0x3e4ccccc` = **0.2f**
    - `0x1ab054` = `0x3ecccccc` = **0.4f**
    - `0x1ab278` = `0x24e69595` ≈ **1e-16**
  **None equals 0.001.** `sleepThreshold=0.001` is NOT confirmed from any available binary (data.unity3d is MISSING; libunity globals are 0.2/0.4/1e-16). Per rule #7 this is documented UNRESOLVED rather than guessed.
- [PARTIAL] wakeCounter semantics — decay path **confirmed** by execution traces (0.4 → 0.39 → 0.38 with dt=0.01 when `wakeCounter >= 0.2f`). Increment/reset path for `wakeCounter < 0.2f` observed under emulation but not fully pinned (approximate object layout). Sleep threshold KE comparison not pinned to a global (`s3` at `0x13ce8b8` returned garbage).
- [DECIDED] `sleepKE` stays **0** (sleep/wake OFF by default). Do NOT change to 0.001 — no binary evidence supports 0.001 as the sleep KE threshold (none of the referenced globals equals 0.001; the KE-threshold constant was not pinned). Per plan rule: sleepKE stays 0 until binary proof exists.
- All T1 config is wired; no further T1 implementation required unless the user requests confirmation of inferred behaviour.