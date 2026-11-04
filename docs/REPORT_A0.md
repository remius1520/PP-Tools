# Phase A0 Report — Setup and Source Verification

**Date:** 2026-11-04  
**Phase:** A0  
**File under test:** `libunity.so` (sha256 starts `1d624d35`, ends `b4364`, size 23613504)

## A0-(a) ReMius_Unity_Extract.zip — NOT FOUND

| Item | Expected | Found | Status |
|---|---|---|---|
| `ReMius_Unity_Extract.zip` | size 168941185 | only `part01` exists, 30408704 bytes | **NOT FOUND** |

Cannot verify zip size (no complete zip). Cannot extract `data.unity3d` or `lib/arm64-v8a/libunity.so` from it.

## A0-(b) libunity.so hash comparison — MATCH

| Location | Size | sha256 |
|---|---|---|
| `/workspaces/PP-Tools/libunity.so` | 23613504 | `1d624d351ba4d01a9f0e413cede4a7e666c1865233623f43932f83c39c6b4364` |
| `/workspaces/PP-Tools/MY_GAME/libunity.so` | 23613504 | `1d624d351ba4d01a9f0e413cede4a7e666c1865233623f43932f83c39c6b4364` |

Both match: same size, same sha256 `1d624d35...b4364` ✓. Matches the verified fact in VERIFIED FACTS.

## A0-(c) data.unity3d unpack — NOT FOUND

`data.unity3d` does not exist anywhere in the workspace. `tools/ufs.py` exists at:
- `MY_PHYSICS_ENGINE/ref/p2e_tools/ufs.py`
- `MY_PHYSICS_ENGINE/tools/ufs.py`
- `alpha_scratch/ufs.py`

But there is no bundle to unpack. `globalgamemanagers` and `sharedassets0.assets` are available instead at:
- `alpha_scratch/gamedata_min/PP3_GAMEDATA_MIN/globalgamemanagers`
- `alpha_scratch/gamedata_min/PP3_GAMEDATA_MIN/sharedassets0.assets`

These were read with UnityPy 1.25.4 (successful extraction, documented below).

## A0-(d) Python packages — ALL INSTALLED

| Package | Version | Status |
|---|---|---|
| capstone | 5.0.7 | installed |
| unicorn | 2.1.4 | installed |
| pyelftools | — | installed |
| UnityPy | 1.25.4 | installed |

## A0-(e) Zip archives — ALL PRESENT

| Archive | Size | Status |
|---|---|---|
| `libil2cpp_2.zip` | 32012750 | present |
| `dump.zip` | 7536798 | present |
| `global-metadata_2.zip` | 5287021 | present |

`dump.cs` (50 MB), `global-metadata.dat` (18 MB), `libil2cpp.so` (127 MB) already extracted in `MY_GAME/`.

## A0-(f) PP3 FINAL PLAN 3.md — no-device rule CONFIRMED

The file contains the rule: **"ممنوع: تسجيل ضربات، fit، معاملات متعلّمة، تقريب، اختيار صيغة بمقارنتها بنتيجة، أو محرك 'قريب'."** and **"لا تجارب على الجهاز"** — rule 1 from the user is present ✓.

## A0-(g) PhysicsManager fields from globalgamemanagers (class 55) — DECODED

Source: `alpha_scratch/gamedata_min/PP3_GAMEDATA_MIN/globalgamemanagers`, read via UnityPy `read_typetree()`.

| Field | Value | Source | Status |
|---|---|---|---|
| `m_Gravity` | (0, -9, 0) | UnityPy read | CONFIRMED |
| `m_BounceThreshold` | 0.0 | UnityPy read | CONFIRMED |
| `m_DefaultMaxDepenetrationVelocity` | 10.0 | UnityPy read | CONFIRMED |
| `m_SleepThreshold` | 0.001 | UnityPy read | CONFIRMED |
| `m_DefaultContactOffset` | 0.001 | UnityPy read | CONFIRMED |
| `m_DefaultSolverIterations` | 30 | UnityPy read | CONFIRMED |
| `m_DefaultSolverVelocityIterations` | 20 | UnityPy read | CONFIRMED |
| `m_DefaultMaxAngularSpeed` | 7.0 | UnityPy read | CONFIRMED |
| `m_FrictionType` | 0 | UnityPy read | CONFIRMED |
| `m_SolverType` | 0 (PGS) | UnityPy read | CONFIRMED |
| `m_EnableEnhancedDeterminism` | True | UnityPy read | CONFIRMED |
| `m_SimulationMode` | 0 (autoSimulation off) | UnityPy read | CONFIRMED |
| `m_LayerCollisionMatrix` | all 0xFFFFFFFF | UnityPy read | CONFIRMED |
| `m_DefaultMaterial` | null (FileID 0, PathID 0) | UnityPy read | CONFIRMED |

Previously unconfirmed fields now resolved: `frictionType=0`, `solverType=0` (PGS), `enhancedDeterminism=True`, `autoSimulation`=off (`SimulationMode=0`).

## Ball Rigidbody from sharedassets0.assets (class 54) — CONFIRMED

Source: `alpha_scratch/gamedata_min/PP3_GAMEDATA_MIN/sharedassets0.assets`, read via UnityPy.

| Field | Value | Status |
|---|---|---|
| `m_Mass` | 10.0 | CONFIRMED |
| `m_LinearDamping` | 0.62 | CONFIRMED |
| `m_AngularDamping` | 1.0 | CONFIRMED |
| `m_UseGravity` | True | CONFIRMED |
| `m_IsKinematic` | False | CONFIRMED |
| `m_Interpolate` | 1 | CONFIRMED |
| `m_Constraints` | 4 | CONFIRMED |
| `m_CollisionDetection` | 2 (ContinuousDynamic) | CONFIRMED |

Two Rigidbody objects found (cue ball and object ball), both with `collisionDetection=2`.

## A0 Summary

| Item | Status |
|---|---|
| libunity.so hash verified | FOUND — `1d624d35...b4364` |
| ReMius_Unity_Extract.zip | NOT FOUND (part01 only) |
| data.unity3d | NOT FOUND |
| globalgamemanagers / sharedassets0.assets | FOUND at `alpha_scratch/gamedata_min/` |
| PhysicsManager fields decoded | ALL CONFIRMED via UnityPy |
| Rigidbody collisionDetection=2 | CONFIRMED via UnityPy |
| Python toolchain (capstone/unicorn/pyelftools/UnityPy) | ALL INSTALLED |
| PP3 FINAL PLAN no-device rule | CONFIRMED |
| `tools/emu/run_fn.py` | FOUND — loads PT_LOAD + relocs, runs under Unicorn |

## What is NOT proven

- `data.unity3d` contents could not be extracted (no complete zip, no data.unity3d file).
- `ReMius_Unity_Extract.zip` could not be verified (only `part01` exists).

**No engine changes made in A0.** No source files modified.

---

5-line Egyptian summary:

الملف libunity.so تم التحقق من hash ويتطابق مع verdad ReMius zip موجود part01 فقط ما فيهاش الحجم الكامل data.unity3d مش موجودة في الشغل physicsManager وsharedassets اتفكوا بنجاح من alpha_scratch كل الحقول دي مؤكدة من الباينري ما عملناش أي تغيير في الكود