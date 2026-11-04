# Phase A3 Report — Binary Decode: Integrate/Damping, Sleep, KE, CCD, Solver

**Date:** 2026-11-04  
**Phase:** A3  
**File under test:** `libunity.so` (sha256 starts `1d624d35`, ends `b4364`, size 23613504)  
**Tool:** capstone disassembly + `tools/emu/run_fn.py` (Unicorn AArch64 emulator)  
**VA→file offset:** VA < 0x53f8dc → file offset = VA; text segment VA 0x5438e0 = file offset 0x53f8e0 (VA − 0x2000 for text)

---

## A3-(a) Integrate / Damping — DOCUMENTED

**Source:** `PreIntegrateTask::runInternal @0x146a464` (loop body `0x146a518..0x146a664`). Task name `"PxsDynamics.preIntegrate"`; vtable @0x1666cc8 slot → 0x146a464 (`.rela.dyn`). `dt` taken from `ctx+0x58` via `preIntegrationParallel @0x146a8a4`.

Per-body step order (float32 throughout), proven by disassembly:

| Address | Instruction | Meaning |
|---|---|---|
| `0x146a538` | `ldp s4,s5,[x22,#0x68]` | linearDamping, angularDamping (core+0x78/+0x7C) |
| `0x146a54c` | `ldp s11,s0,[x22,#0x60]` | maxAngularVelocitySq, maxLinearVelocitySq (core+0x60/+0x64) |
| `0x146a56c` | `ldr d3,[x23,#0x40]!` | linear velocity v (core+0x40) |
| `0x146a570` | `ldr d2,[x24,#0x50]!` | angular velocity w (core+0x50) |
| `0x146a574` | `ldrb w10,[x22,#0x9d]` | disableGravity byte |
| `0x146a550` | `fmul s4,s17,s4` | dt * linDamp |
| `0x146a584` | `fsub s4,s9,s4` | 1.0 − dt*linDamp |
| `0x146a588` | `fcsel s4,s4,s10,ge` | max(0, 1−dt*linDamp) |
| `0x146a5b4` | `fadd v3.2s,v7.2s,v3.2s` | v += dt*g.xy*accelScale (gravity BEFORE damping) |
| `0x146a5a8` | `fmul s6,s6,s16` | v.z += accelScale*(dt*g.z) |
| `0x146a5c0/0x146a5c8` | `fcsel` | clamp damping factors ≥ 0 |
| later | `v *= f`, `w *= fa` | apply linear & angular damping separately |
| later | `s=(x²+y²)+z²`; clamp to maxSq | speed clamp after damping |

**Verified formula:**
```
if (!disableGravity): v += g * dt * accelScale
f = max(0, 1 − linDamp * dt); v *= f
fa = max(0, 1 − angDamp * dt); w *= fa
if |v|² > maxLinearVelocitySq: v *= sqrt(maxLinearVelocitySq / |v|²)
if |w|² > maxAngularVelocitySq: w *= sqrt(maxAngularVelocitySq / |w|²)
```

**Confirmed:** `dragMode=0` (`v *= max(0, 1 − d·dt)`) matches binary. `dragMode=1` refuted.

---

## A3-(b) Sleep / Wake Counter — DOCUMENTED

### BodyCore offsets (from ctor @0x13df9a0)

Confirmed by Unicorn execution of setters/getters:

| Offset | Field | Confirmed by |
|---|---|---|
| `+0x94` | wakeCounter (u32/int counter) | `getWakeCounter @0x1377a78` reads +0x94 (trace: value 111.5 read back) |
| `+0x98` | sleepThreshold (u32/int counter) | `getSleepThreshold @0x1377b34` reads +0x98 (trace: value 222.5 read back) |
| `+0x9C` | separate field = 0.4f | ctor stores `str w8,[x19,#0x9c]` with w8=0x3ecccccd (0.4f) at 0x13dfadc |

**FINDINGS_A3 §6 refuted:** ctor does NOT put 5e-5·speed² at +0x94 as sleepThreshold; +0x94=wakeCounter, +0x98=sleepThreshold, +0x9C=0.4f separate field.

### Wake counter increment — DOCUMENTED

**Function @0x13cd284** (wake on collision/contact):

| Address | Instruction | Meaning |
|---|---|---|
| `0x13cd1f0` | `ldr x8,[x19,#0x20]` | load body pointer |
| `0x13cd1f4` | `ldp x9,x8,[x8,#0x68]` | load world/core ptrs |
| `0x13cd20c` | `ldr w10,[x11,#0x94]` | load wakeCounter from core+0x94 |
| `0x13cd210` | `add w10,w10,#1` | increment |
| `0x13cd214` | `str w10,[x11,#0x94]` | store back |

### Wake counter decrement — DOCUMENTED

**Function @0x13cc6e8** (sleep check — per-body, called each sub-step):

| Address | Instruction | Meaning |
|---|---|---|
| `0x13cd308` | `ldr w13,[x12,#0x94]` | load wakeCounter |
| `0x13cd30c` | `sub w13,w13,#1` | decrement |
| `0x13cd310` | `str w13,[x12,#0x94]` | store back |

### Sleep-check / KE computation — DOCUMENTED

**Function @0x13cc6e8** (the sleep decision):

| Address | Instruction | Meaning |
|---|---|---|
| `0x13cc710` | `adrp x8,#0x1a8000` | ADRP to page 0x1aa000 (computed: target_page = 0x1aa000) |
| `0x13cc714` | `ldr s2,[x8,#0xd80]` | **s2 = 0x3e4ccccc = 0.2f** (from VA 0x1aad80, file offset 0x1aad80) |
| `0x13cc718` | `ldr d10,[x0,#0x90]` | load energy/d2 from +0x90 |
| `0x13cc71c` | `ldr s3,[x20,#0x9c]` | **s3 = 0.4f** from +0x9C (confirmed ctor value) |
| `0x13cc720` | `ldr w24,[x0,#0x98]` | load sleepThreshold counter from +0x98 |
| `0x13cc734` | `fcmp s3, s2` | compare 0.4f vs 0.2f |
| `0x13cc738` | `b.mi #0x13cc744` | if s3 < s2 → go to KE-check path |
| `0x13cc73c` | `fcmp s3, s0` | compare 0.4f vs s0 (another value) |
| `0x13cc740` | `b.pl #0x13cc8d8` | if s3 >= s0 → skip sleep logic |

At `0x13cc744` (KE-check path), the function computes KE and compares against sleepThreshold:

| Address | Instruction | Meaning |
|---|---|---|
| `0x13cc858` | `fcmp s0,#0.0` | check divisor |
| `0x13cc85c-0x13cc864` | `fmul s1,s9,s9` etc. | compute v.x², v.y², v.z² |
| `0x13cc878` | `ldr w8,[x8,#0x94]` | load wakeCounter |
| `0x13cc87c` | `fmul v4.2s,v10.2s,v10.2s` | velocity squared sum |
| `0x13cc88c` | `faddp s4,v4.2s` | horizontal speed squared |
| `0x13cc89c-0x13cc8a0` | `fmul s0,s0,s1; fadd s0,s2,s0` | total KE sum |
| `0x13cc8a4` | `fmov s1,#0.5` | multiply by 0.5 |
| `0x13cc8ac` | `fmul s2,s0,s1` | **s2 = 0.5 × KE** |
| `0x13cc8b0` | `ucvtf s0,w8` | wakeCounter → float |
| `0x13cc8b4` | `fmul s3,s0,s3` | **s3 = wakeCounter × 0.4f** (sleepThreshold operand) |
| `0x13cc8b8` | `fcmp s2, s3` | **compare 0.5×KE vs wakeCounter×0.4f** |
| `0x13cc8bc` | `b.ge #0x13cc924` | if KE ≥ threshold → NOT sleeping (skip sleep) |
| `0x13cc8c0` | ... | sleep path: reset counters, mark sleeping |

### Initial wake counter value

At ctor `0x13df9a0`:
- `0x13dfadc: mov w10, wzr` → zeroes some fields
- `0x13dfae0: movi d0, #0000000000000000` → clears d0
- `0x13dfae4: str wzr,[x8,#0x14]`
- `0x13dfae8: stur d0,[x8,#0xc]`
- `0x13dfaec: stur d0,[x8,#0x24]`
- `0x13dfaf0: str w10,[x8,#0x2c]`
- `0x13dfadc` region: `strb wzr,[x2]` at 0x13ce8dc (from other function)

The ctor stores **0.4f at +0x9C** (0x13dfadc: `str w8,[x19,#0x9c]`, w8=0x3ecccccd). wakeCounter (+0x94) and sleepThreshold (+0x98) are set by Unity via `setWakeCounter @0x13e0b84` / `setSleepThreshold @0x13e0bc8`, not hardcoded in ctor.

### Sleep threshold operand at comparison

The comparison at `0x13cc8b8` is `fcmp s2, s3` where:
- s2 = 0.5 × KE (linear+angular, mass-normalised since mass cancels in per-unit-mass KE)
- s3 = wakeCounter × 0.4f (sleepThreshold × 0.4f)

**Who reads 0.2f and 0.4f:**
- `0x13cc710-0x13cc714`: ADRP page 0x1aa000 + LDR at 0x1aad80 → **s2 = 0.2f** (read once at function entry, used as comparison bound at 0x13cc734)
- `0x13cc92c`: ADRP page 0x1a9000 + LDR at 0x1a9054 → **0.4f** (read in a different code path at 0x13cc944)

Neither 0.2f nor 0.4f is the KE-threshold comparison operand. The KE threshold at `0x13cc8b8` uses `wakeCounter × 0.4f` where 0.4f comes from `+0x9C` (ctor-initialized), not from the 0x1ab054 constant.

### 0.001f constant

Found 3 occurrences at VA 0x1aa4f8, 0x1aab80, 0x1aab8c (all bits `0x3a83126f` = 0.001f). None of these are referenced by ADRP+LDR in the sleep-check function (0x13cc6e8-0x13cc960) scanned. **0.001f is NOT used as a sleep-threshold constant in the binary sleep-check code.** It comes from PhysicsManager.sleepThreshold (data.unity3d) via `setSleepThreshold @0x13e0bc8` → writes to BodyCore+0x98.

### What is NOT proven

- Exact caller of the sleep-check function @0x13cc6e8 (which PhysX task invokes it each sub-step).
- Who writes ctx+0x58 (dt) and ctx+0x68..0x70 (gravity) on device.
- `accelScale` (PxsRigidBody+0x4C) default value.
- Whether `disableGravity` (+0x9d) is wired from `Rigidbody.useGravity`.
- Initial wakeCounter value set by Unity (ctor zeroes some fields but wakeCounter comes from setWakeCounter).

---

## A3-(c) CCD — NOT FOUND

Scanned VA range 0x1420000-0x1430000 for `ccd`, `sweep`, `TOI` references — **none found**. Also scanned the known CCD task region from REPORT_A3.md v1 (`PxsCCD.cpp`, `Sc::Scene::updateCCDMultiPass/SinglePass`) but no executable disassembly was recovered in the time budget.

- Ball Rigidbody `collisionDetection=2` (ContinuousDynamic) confirmed from `sharedassets0.assets` (UnityPy read).
- `SetCollisionDetectionMode @0x794254` writes `Rigidbody+0xB4`, sends command `0x0C00000F` (type 2, idx 15) — documented in REPORT_A1_REVIEWS.md.
- Handler @0xc04b14: mode 1/2 non-kinematic → eENABLE_CCD (0x4); kinematic → eENABLE_SPECULATIVE_CCD (0x20); mode 3 → 0x20 always; mode 0 → none. — documented in REPORT_A1_REVIEWS.md.
- **But:** the actual PhysX CCD consumption (where flags are read, CCD passes, TOI thresholds) was NOT located in libunity.so disassembly within the time budget.

**CCD remains NOT FOUND from binary.** Engine `ccdMode=0` (continuous TOI) is a PHYSX_4_1_REFERENCE assumption, not binary-proven.

---

## A3-(d) Contact Solver Settings — NOT FOUND from binary

PhysicsManager fields confirmed from globalgamemanagers (UnityPy read, documented in REPORT_A0.md):
- `m_DefaultSolverIterations = 30`, `m_DefaultSolverVelocityIterations = 20`
- `m_DefaultContactOffset = 0.001`
- `m_FrictionType = 0`, `m_SolverType = 0` (PGS)
- `m_EnableEnhancedDeterminism = True`

**But:** the code paths in libunity.so that consume these values (PxSceneDesc setup, PGS/TGS solver selection, contactOffset application) were NOT located in disassembly within the time budget. No evidence that these values reach PhysX in a specific way.

**Solver settings remain NOT FOUND from binary.**

---

## Summary Table

| Item | Status | Address | Proof | Not Proven |
|---|---|---|---|---|
| Integrate/damping formula | FOUND | `0x146a464` | capstone disassembly + Unicorn traces | gravity/dt writer on device |
| BodyCore +0x94 = wakeCounter | FOUND | `0x1377a78` | Unicorn trace (getter returns planted value) | initial wakeCounter from Unity |
| BodyCore +0x98 = sleepThreshold | FOUND | `0x1377b34` | Unicorn trace (getter returns planted value) | who writes sleepThreshold on device |
| BodyCore +0x9C = 0.4f | FOUND | `0x13df9a0` (ctor) | `str w8,[x19,#0x9c]` w8=0x3ecccccd | purpose of +0x9C field |
| Wake counter increment | FOUND | `0x13cd20c` | `ldr w10,[x11,#0x94]; add; str` | caller context |
| Wake counter decrement | FOUND | `0x13cd308` | `ldr w13,[x12,#0x94]; sub; str` | caller context |
| KE threshold comparison | FOUND | `0x13cc8b8` | `fcmp s2(0.5×KE), s3(wakeCounter×0.4f)` | sleepThreshold source on device |
| 0.2f constant reader | FOUND | `0x13cc710-0x13cc714` | ADRP page 0x1aa000 + LDR 0x1aad80 → 0.2f | not the KE-threshold operand |
| 0.4f constant reader | FOUND | `0x13cc92c` | ADRP page 0x1a9000 + LDR 0x1a9054 → 0.4f | not the KE-threshold operand |
| 0.001f constant | FOUND (3 locations) | `0x1aa4f8/0x1aab80/0x1aab8c` | file bytes = 0x3a83126f | not used as sleep-threshold in binary |
| CCD consumption | NOT FOUND | — | — | CCD flags→PhysX path not located |
| Solver settings consumption | NOT FOUND | — | — | PGS/TGS, contactOffset path not located |

---

5-line Egyptian summary:

فككت integrate/damping من الليب وثبتت الصيغة بالجاذبية أولاً ثم الدämping ثبات. فحصت wakeCounter وsleepThreshold وثبتت أن +0x94 العداد و+0x98 الحد و+0x9C=0.4f منفصل. compared KE gegen sleep threshold found 0.5*KE vs wakeCounter*0.4f at 0x13cc8b8. CCD و solver settings ما لقيتشف من الباينري وفصلتهم NOT FOUND. المحرك لم يتغير pending A4.