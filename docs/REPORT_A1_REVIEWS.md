# A1 Review Summaries (<=10 lines per archive)

## A — SLEEP (PP3_A_v1 / SLEEP_FINDINGS.md + SLEEP_TEST_PLAN.md)
- Sleep commands flow Unity Rigidbody.Sleep/IsSleeping → command → handler → NpRigidDynamic vtable → Sc::BodyCore.
- `Rigidbody::Sleep @0x7941fc` sends command `0x0C00002C` (type 2, idx 44); handler @0xc05764 checks kinematic via vtable+0x1b0, errors or calls vtable+0x280 (putToSleep @0x1377cc4).
- `IsSleeping @0x13779cc` = `(*(u32*)(this+0x178)) != 0` (vtable+0x228).
- vtable slots confirmed: setWakeCounter @0x13779dc (+0x230), getWakeCounter @0x1377a78 (+0x238), setSleepThreshold @0x1377a98 (+0x240), getSleepThreshold @0x1377b34 (+0x248).
- Sc::BodyCore: wakeCounter +0x94, sleepThreshold +0x98 (Np reads them at +0x114/+0x118; core at Np+0x80).
- Sleep-check function, energy formula, initial wakeCounter, default sleepThreshold = NOT found / OPEN.
- `sleepKE=0` locked; no PP2 change from this chat.
- Violations: none (all documented from binary; no fitted constants).

## A1 — Strike sequence / who computes motion (FINDINGS_A1.md)
- `ServerTracker.FixedUpdate @0x5DB99F4` runs `OnServerSimulate @0x5DB856C` only if `byte[this+0x3A] == IsServer` (dump.cs: 0x3A).
- Shot sequence: `OnRequestShotServer @0x5DB70C0` → baseline → `OnCastCueBall @0x5DD6BB8` (SphereCast) → `OnInitalizeSimulation @0x5DB7390` (set isKinematic=false, set isTrigger=false, AwakeBalls WakeUp non-InTube, isHit=true).
- `Physics.Simulate` runs each server FixedUpdate; special path (7 sub-steps, snap, ApplySpin) otherwise.
- End-of-shot: `IsMoving(≥0.002) || IsOverlapping || IsBallInsidePocket` → isHit=false, SleepBalls().
- G1–G6 gaps between game and live predictor (G1 trigger/WakeUp not replicated; G2 client vs server; G3 OverlapSphere first contact; G4 InTube made dynamic; G5 stop predicate; G6 Simulate steps all dynamic).
- 5 physics callbacks: Ball.OnTriggerEnter 0x5DD1F40, Ball.OnCollisionEnter 0x5DD38B8, Ball.OnCollisionExit 0x5DD5B78, CueBall.OnCollisionEnter 0x5DD9B7C, PocketDebugger.OnTriggerEnter 0x346EBA8; expected gated N≈5.
- Violations: none; client/server split is a strong inference, not yet device-verified (needs D1–D5 logs).

## A3 — Integrate/damping (FINDINGS_A3.md)
- `PreIntegrateTask::runInternal @0x146a464` (loop 0x146a518..0x146a664): per-body order = gravity → `v *= max(0, 1 − d·dt)` → clamp to maxSq.
- float32 throughout; `dt = ctx+0x58`, gravity `ctx+0x68..0x70`, damping `core+0x78/+0x7C`.
- `PxsBodyCore` map from BodyCore ctor @0x13df9a0: linearDamping+0x78, angularDamping+0x7C, maxLinearVelocitySq+0x74, maxAngularVelocitySq+0x70.
- PP2 `dragMode=0` matches binary; `dragMode=1` (`v/=(1+d·dt)`) refuted.
- float32 vs double PP2 position error ≤ 0.41 mm even at 32 m/s — not source of 5 cm discrepancy.
- `src/pp2_integrate_f32.hpp` + `tests/test_integrate_f32.cpp` (ALL OK) written but not wired into engine.
- OPEN: exact gravity/dt writer on device, accelScale default, max velocity clamping, disableGravity flag.
- Violations: none; PHYSX_4_1_REFERENCE knowledge tagged, not default-on.

## A4 — CCD (FINDINGS_A4.md v2 + CCD_DECOMP_GHIDRA_v2.txt)
- CCD path exists: `PxsCCD.cpp`, `Sc::Scene::updateCCDMultiPass/SinglePass`, `postCCDPass`, `ccdBroadPhase`, `UpdateCCDBoundsTask`.
- `SetCollisionDetectionMode @0x794254` writes `Rigidbody+0xB4`, sends command `0x0C00000F` (type 2, idx 15).
- Handler @0xc04b14: mode 1/2 non-kinematic → eENABLE_CCD (0x4); kinematic → eENABLE_SPECULATIVE_CCD (0x20); mode 3 → 0x20 always; mode 0 → none.
- `setRigidBodyFlags` (vtable+0x1a8 @0x137a04c) rejects KINEMATIC|CCD and CCD|SPECULATIVE combos.
- `ccdMaxPasses` stored at `PxsCCDContext+0x1B4` (Sc::Scene+0x748); current pass `ctx+0xB4`.
- CCD task member functions mapped to 10 addresses; sweep/TOI/stencil not decompiled (no IDA/Ghidra available here).
- OPEN: actual Unity PxSceneDesc flags/ccdMaxPasses/ccdThreshold, filter shader, fast-moving criterion, CCD effect on restitution.
- No PP2 change; `ccdMode` key proposed but not added.
- Violations: none; CCD "discrete/continuous" addition has no binary proof yet.

## A4_v3 — HANDOFF update
- CCD_DECOMP_GHIDRA_v2.txt: first file was invalid (Ghida image base 0x100000; VA+0x100000 = correct). 27 functions decompiled.
- No IDA available; fallback is CCD_TEST_PLAN.md C1/C2 via T3.

## T1_REVIEW (REVIEW_T1.md)
- Built before/after with `g++`, `sh build.sh`; all tests passed (13/13, 10/10, 96, 24, 22, 8, 18, 16, 5607, 24, 789, 78, 8, 15/15, api v3, 24, 12).
- Compared before/after on 120 random break shots (force 2..36, table level11): `maxDepenetrationVelocity = 10` changed 55/120 high-force shots by up to 1.9 m — invented behavior.
- Fix: default `maxDepenetrationVelocity = 0`; after change 0 difference from 120 vs old.
- `sleepKE=0` locked; damping/sleep/CCD are PHYSX_4_1_REFERENCE, not decompiled from libunity.
- `F6` in FINDINGS_T1: 1 mm accuracy not currently achievable.
- Violations: none; "matches/exact" language avoided.

## CCD_DECOMP_GHIDRA_v2.txt
- 5064 lines; GHIDRA VA = VA+0x100000.
- Decompiled `FUN_014b7484` (pass counter / isLastPass logic at Sc::Scene+0x748, ctx+0xB4, ctx+0x1B4) and `FUN_0150b9c0`.
- isLastPass = `(ctx+0xB4 == ctx+0x1B4 − 1)`; pass tasks chained via vtable+0x20/+0x28 calls.
- Sweep/TOI/stencil functions around 0x1423bf0/0x1424804/0x1424f18/0x13b344c/0x13ce5a8 identified by name only.
- Violations: none.

## Recorder Audit Bundle (AUDIT_REPORT.md + pp2_shot 6.txt)
- All 16 balls kinematic on client (`kinematicAtLastIdleSnapshot=16/16`); `simulationMode=0` (FixedUpdate).
- All Rigidbody velocities zero — `get_velocity` and `Time.fixedTime` never resolved (`cv2` and `T` absent).
- PRED is stale: attached to previous shot (F=1.522) not the recorded shot (F=2.786); `runLive()` requires 0.8s settle.
- F frames are mixed FixedUpdate/LateUpdate, not pure physics ticks; only 19% carry ball data.
- PRE/PREDBALL indexing diverges (ball 10 missing from PRE, ball 11 missing from PREDBALL).
- No collision events recorded during actual shot; PREDEV only from prediction.
- Deterministic comparison design possible using SHOT (F=2.786) + PRE positions vs POST positions.
- Violations: the audit itself is read-only documentation; no source modified. PRED staleness means recorded shots must NOT be used for engine comparison without correction.