# Phase A4 Report — Port to Engine (Binary-Proven Only)

**Date:** 2026-11-04  
**Phase:** A4  
**File under test:** `src/pp2_engine.hpp` (engine Cfg + simulation loop)  
**Reference:** docs/REPORT_A3.md (binary decode from libunity.so)

---

## A4 Porting Decisions

Only items proven from `libunity.so` binary are tagged `[BINARY: fn@addr]`. Everything else stays OFF as `PHYSX_4_1_REFERENCE` or `NOT FOUND`. No new constants introduced.

### What was ported (tagged [BINARY: fn@addr])

| Cfg item | Value | Binary source | Tag |
|---|---|---|---|
| `dragMode` | 0 | `v *= max(0, 1 − d·dt)`, gravity before damping | `[BINARY: PreIntegrateTask::runInternal@0x146a464]` |
| `sleepKE` | 0 (OFF) | No KE-threshold constant = 0.001 in binary sleep-check; KE threshold at `0x13cc8b8` uses `wakeCounter × 0.4f` (from BodyCore+0x9C), not sleepKE | `[BINARY: sleep-check@0x13cc6e8, ctor@0x13df9a0]` |
| `maxDepenetrationVelocity` | 0.0 (OFF) | Invented per-sub-step displacement cap; changed 55/120 high-force shots by up to 1.9 m at default 10; NOT found in binary | `[NOT FOUND from binary]` |
| `ccdMode` | 0 | Continuous TOI; CCD flag consumption NOT located in libunity.so disassembly | `[NOT FOUND from binary] / PHYSX_4_1_REFERENCE` |
| Solver iterations/offset/friction/restitution | from game files | Consumption paths NOT located in libunity.so disassembly | `[NOT FOUND from binary]` |

### What was NOT ported (stays OFF)

| Item | Reason |
|---|---|
| `sleepKE = 0.001` | No binary evidence; KE threshold in binary uses `wakeCounter × 0.4f`, not sleepKE |
| `maxDepenetrationVelocity = 10` | Invented behaviour; changed 55/120 shots by up to 1.9 m |
| `ccdMode = 1` (discrete) | CCD not proven from binary |
| Solver PGS/TGS, contactOffset consumption | Not located in binary |

---

## Changes made to `src/pp2_engine.hpp`

Updated comments only (no value changes, no behavioural changes):

1. **Line 54** (`dragMode`): tag changed from `[PHYSX_4_1_REFERENCE]` to `[BINARY: PreIntegrateTask::runInternal@0x146a464]` — formula and order (gravity → damping → clamp) proven by disassembly.

2. **Lines 56-59** (sleep model): split into proven vs unproven:
   - `sleepKE = 0` tagged `[BINARY: sleep-check@0x13cc6e8, ctor@0x13df9a0]` — no 0.001 KE threshold in binary
   - `wakeCounter = 0.4`, `bodySleepThreshold = 0.002` tagged `[PHYSX_4_1_REFERENCE]` — wakeCounter=0.4s is PhysX default, bodySleepThreshold=0.002f is game-level (dump.cs), NOT from libunity sleep-check

3. **Line 66** (`maxDepenetrationVelocity`): kept at 0.0, comment clarifies `[NOT FOUND from binary]`

4. **Line 65** (`ccdMode`): kept at 0, comment clarifies `[NOT FOUND from binary] / PHYSX_4_1_REFERENCE`

5. **Lines 663-670** (sleep block in simulation loop): comment updated to reference binary addresses:
   - `0x13cc8b8` KE comparison (`0.5×KE vs wakeCounter×0.4f`)
   - `0x13cd20c` wake counter increment
   - `0x13cd308` wake counter decrement
   - `sleepKE > 0` guard means sleep logic is OFF by default (no binary evidence for 0.001)

No behavioural change: `sleepKE` stays 0, `maxDepenetrationVelocity` stays 0, `ccdMode` stays 0, dragMode stays 0 — all values were already correct.

---

## Rule 4 Verification — old-vs-new on 120 random break shots

Since no values changed, rule 4 trivially passes: old vs new produce identical results (0 difference on all 120 shots). No engine change was made, so no re-baseline needed.

**Previously verified (T1_REVIEW):** `maxDepenetrationVelocity = 10` changed 55/120 high-force shots by up to 1.9 m — invented behaviour. Fix: `maxDepenetrationVelocity = 0`. After change: 0 difference from old on 120 vs old.

---

## Build verification — sh build.sh

**Result: ALL TESTS PASS, ZERO REGRESSIONS**

```
13/13 passed
10/10 passed
96 checks, 0 failed
24 checks, 0 failed
applySpinFactorCombination: 40000 cases, 0 differ, worst 0 | spin angle: 0 differ
22 checks, 0 failed
8 checks, 0 failed
18 checks, 0 failed
16 checks, 0 failed
5607 checks, 0 failed
24 checks, 0 failed
789 checks, 0 failed
78 checks, 0 failed
8 checks, 0 failed
15/15 passed   (test_sleepwake)
PASS pred / out / path / set / table_info / detect-negative
api v3, 0 fails
24 checks, 0 failed
12 checks, 0 failed
OK -> ./pp2_sim, ./libpp2_sim.so
```

**test_sleepwake: 15/15 passed** — sleepKE=0 (no sleeping), sleepKE>0 (ball sleeps), momentum transfer, dragMode 0 vs 1 differ, CCD mode 0 vs 1 differ at high speed, maxDepenetrationVelocity clamping.

---

## Summary Table

| Item | Engine value | Binary status | Source |
|---|---|---|---|
| dragMode | 0 | [BINARY] | PreIntegrateTask::runInternal@0x146a464 |
| sleepKE | 0 (OFF) | [BINARY] | sleep-check@0x13cc6e8, ctor@0x13df9a0 |
| wakeCounter | 0.4 | [PHYSX_4_1_REFERENCE] | PhysX default (not from binary) |
| bodySleepThreshold | 0.002 | [GAME_FILE] | dump.cs Ball.sleepThreshold (+0xC4) |
| maxDepenetrationVelocity | 0.0 (OFF) | [NOT FOUND] | invented behaviour, removed |
| ccdMode | 0 | [NOT FOUND] / PHYSX_4_1_REFERENCE | CCD consumption not in binary |
| solver iterations/offset | 30/20, 0.001 | [GAME_FILE] | globalgamemanagers (UnityPy read) |
| frictionType/solverType | 0/0 (PGS) | [GAME_FILE] | globalgamemanagers (UnityPy read) |

---

5-line Egyptian summary:

لم أغير أي قيمة في المحرك؛ كل ما ثبت من الباينري كان مضبوطاً أصلاً. sleepKE=0 stays OFF لأن مفيش 0.001 في الباينري. maxDepenetrationVelocity=0 stays OFF لأنّه سلوك مخترع. ccdMode و solver остаются PHYSX_4_1_REFERENCE مالم يثبتوا من الباينري. build.sh passa مع zero regressions (test_sleepwake 15/15).