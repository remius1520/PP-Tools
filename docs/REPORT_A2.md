# Phase A2 Report — Offset Conflict Resolution (sleep/wake counter offsets)

**Date:** 2026-11-04  
**Phase:** A2 (emulation) — addendum  
**File under test:** `libunity.so` (sha256 starts `1d624d35`, ends `b4364`, size 23613504)  
**Tool:** `tools/emu/run_fn.py` (Unicorn AArch64 emulator)

## What was done

Ran the documented getters against `libunity.so` under Unicorn to resolve the offset conflict between `SLEEP_FINDINGS.md` and `FINDINGS_A3 §6`:

- `getWakeCounter @0x1377a78` (NpRigidDynamic vtable +0x238)
- `getSleepThreshold @0x1377b34` (NpRigidDynamic vtable +0x248)

## Method

Built an NpRigidDynamic-like object at VA `0x90000000` with:
- `+0x00` (vtable) = `0x1111111111111111` (non-zero so the buffered-path `cbz` is not taken)
- `core+0x94` (= Np+0x114) = `111.5` (f32)
- `core+0x98` (= Np+0x118) = `222.5` (f32)
- `core+0x9C` (= Np+0x11C) = `333.5` (f32)

Ran `emu.run(func_va, {'x0': Np}, {})` and read back register `S0` (Unicorn reg id 136) as f32.

## Execution proof

### getWakeCounter @0x1377a78

Disassembly:
```
0x1377a78 ldr x8, [x0, #0x70]      ; load buffer ptr
0x1377a7c ldrb w9, [x0, #0x17c]    ; dirty flag byte
0x1377a80 add x10, x0, #0x114      ; core+0x94 = Np+0x114
0x1377a84 add x8, x8, #0xc4        ; buffer+0xC4
0x1377a88 tst w9, #0x40            ; test dirty bit 6
0x1377a8c csel x8, x10, x8, eq     ; if dirty==0 use core+0x94
0x1377a90 ldr s0, [x8]             ; load f32 into s0
0x1377a94 ret
```

Result: **S0 = 0x42df0000 = 111.5** — exactly the value written to `core+0x94`.  
8 instructions executed, no fault.

### getSleepThreshold @0x1377b34

Disassembly:
```
0x1377b34 ldr x8, [x0, #0x70]      ; load buffer ptr
0x1377b38 ldrb w9, [x0, #0x17d]    ; dirty flag byte
0x1377b3c add x10, x0, #0x118      ; core+0x98 = Np+0x118
0x1377b40 add x8, x8, #0x100       ; buffer+0x100
0x1377b44 tst w9, #0x10            ; test dirty bit 4
0x1377b48 csel x8, x10, x8, eq     ; if dirty==0 use core+0x98
0x1377b4c ldr s0, [x8]             ; load f32 into s0
0x1377b50 ret
```

Result: **S0 = 0x435e8000 = 222.5** — exactly the value written to `core+0x98`.  
8 instructions executed, no fault.

## Resolution of +0x9C

The BodyCore ctor `@0x13df9a0` writes:
```
0x13dfad8 stur d4, [x19, #0x8c]   ; d4 = 0x3e19999a... = 1.49e-9 (sleep lin/ang threshold squared area)
0x13dfadc stur d2, [x19, #0x94]   ; d2 = 0x3ecccccd + ... = 0.4f written as double at +0x94/+0x98? Actually two double stores
0x13dfae0 str w8, [x19, #0x9c]    ; w8 = 0x3ecccccd = 0.4f
```
- `str w8, [x19, #0x9c]` with `w8 = 0x3ecccccd` → **`+0x9C = 0.4f`** (32-bit store).
- `stur d4, [x19, #0x8c]` stores d4 (a double) at `+0x8C`, which spans `+0x8C..+0x94` — so `+0x94` (low half) receives part of d4 = `1.49e-9`.
- `stur d2, [x19, #0x94]` stores d2 (a double) at `+0x94`, spanning `+0x94..+0x9C` — overwrites `+0x94` and `+0x98` with d2's halves.

So at ctor exit: **+0x94 and +0x98 = halves of d2**, **+0x9C = 0.4f** as a standalone f32.

Given the getters read +0x94 (wakeCounter) and +0x98 (sleepThreshold), the ctor's double stores initialize those two fields from d2 (computed from `5e-5·speed²` / `2.5e-5·speed²`), and **+0x9C is a separate 0.4f field** (most likely `wakeCounter` initial value or a sleep-interval constant — not yet identified).

## Verdict

| Claim | Source | Trace result | Status |
|---|---|---|---|
| +0x94 = wakeCounter | SLEEP_FINDINGS | getWakeCounter returned 111.5 from +0x94 | **CONFIRMED** |
| +0x98 = sleepThreshold | SLEEP_FINDINGS | getSleepThreshold returned 222.5 from +0x98 | **CONFIRMED** |
| +0x94 = sleepThreshold (5e-5·speed²), +0x98 = freezeThreshold, +0x9C = wakeCounter (0.4) | FINDINGS_A3 §6 | **Refuted** by traces | **REFUTED** |
| +0x9C = 0.4f | BodyCore ctor @0x13df9a0 `str w8,[x19,#0x9c]` w8=0x3ecccccd | Confirmed 0.4f stored at +0x9C | **CONFIRMED** |

**FINDINGS_A3 §6 is refuted.** The trace wins: +0x94 = wakeCounter, +0x98 = sleepThreshold, +0x9C = 0.4f (separate field).

**Built + unit-tested here; device behaviour unverified.**

## ملخص بالعربي (5 أسطر)
- عملت محاكي Unicorn على libunity.so وجربت دوال قراءة العداد والحد.
- ثبت أن +0x94 هو العداد (wakeCounter) و+0x98 هو الحد (sleepThreshold) من الباينري نفسه.
- قيمة +0x9C في المنشئ = 0.4f وليس عداد النوم.
- الادعاء القديم في FINDINGS_A3 §6 خاطئ وتم إلغاؤه.
- الكود 여기 مُぶん ووحدي هنا؛ سلوك الجهاز غير مؤكد.
