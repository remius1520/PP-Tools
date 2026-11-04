#!/usr/bin/env python3
"""Trace Unity/libunity.so updateWakeCounter (0x13ce6e8) under Unicorn.

Goal: determine the exact sleep KE threshold constant and wakeCounter
increment/decay/sleep semantics from real execution.

Memory map (from readelf -l libunity.so):
  LOAD0: file 0x000000 -> va 0x00000000, size 0x53f8dc  (R)  -> globals at 0x1aad80, 0x1ab054, 0x1ab278
  LOAD1: file 0x53f8e0 -> va 0x5438e0, size 0x10d8100 (R X) -> .text incl. 0x13ce6e8
  LOAD2: file 0x16179e0 -> va 0x161f9e0, size 0x054620 (RW) -> .data.rel.ro
  LOAD3: file 0x166bd00 -> va 0x1677d00, size 0x016db30 (RW) -> .data + .bss

updateWakeCounter(x0=wrapper, x1=vel/transform ptr, s0=float arg):
  x20 = [x0+0x50]  -> BodyCore*  (wakeCounter at BodyCore+0x9c, init 0.4f)
  s2  = G1 @0x1aad80 = 0.2f
  if wakeCounter < 0.2f: increment path (wakeCounter += dt)
  else: fcmp wakeCounter vs s0(arg); if wakeCounter >= s0: sleep/decay path
"""
import struct, sys
from unicorn import *
from unicorn.arm64_const import *

SO = '/workspaces/PP-Tools/MY_GAME/libunity.so'
FUNC = 0x13ce6e8

SEGS = [
    (0x00000000, 0x00000000, 0x53f8dc, UC_PROT_READ),
    (0x53f8e0, 0x5438e0, 0x10d8100, UC_PROT_READ | UC_PROT_EXEC),
    (0x16179e0, 0x161f9e0, 0x054620, UC_PROT_READ | UC_PROT_WRITE),
    (0x166bd00, 0x1677d00, 0x016db30, UC_PROT_READ | UC_PROT_WRITE),
]
STACK_BASE = 0x2000000000
STACK_SIZE = 0x100000
HEAP_BASE = 0x30000000
HEAP_SIZE = 0x100000


def f32(b): return struct.unpack('<f', b)[0]


def map_uc(uc, so):
    with open(so, 'rb') as f:
        data = f.read()
    for fo, va0, sz, prot in SEGS:
        msz = ((sz + 0xfff) & ~0xfff)
        uc.mem_map(va0 & ~0xfff, msz, prot)
        chunk = data[fo:fo + sz]
        if chunk:
            uc.mem_write(va0, chunk)
    uc.mem_map(STACK_BASE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
    uc.mem_map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE)


def build_mem(wakeCounter=0.4, vel=(1.0, 0.0, 0.0), ang=0.0, dt=0.01,
              flags=0, body8c=1.0):
    """Build heap object bytes. Returns (mem, w, b, v) absolute addresses."""
    w = HEAP_BASE + 0x0000        # wrapper object
    b = HEAP_BASE + 0x1000        # BodyCore
    v = HEAP_BASE + 0x2000        # velocity/transform arg (x1)
    fl = HEAP_BASE + 0x3000       # flags object pointed by [wrapper+0x88]
    mem = bytearray(HEAP_SIZE)

    def w32(off, val): struct.pack_into('<I', mem, off - HEAP_BASE, val)
    def w64(off, val): struct.pack_into('<Q', mem, off - HEAP_BASE, val)
    def wf(off, val): struct.pack_into('<f', mem, off - HEAP_BASE, val)
    def wd(off, val): struct.pack_into('<d', mem, off - HEAP_BASE, val)

    w64(w + 0x50, b)                                  # +0x50 = BodyCore ptr
    w64(w + 0x88, fl)                                 # +0x88 = flags obj ptr
    wd(w + 0x90, 0.0)                                 # +0x90 (saved d10)
    wf(w + 0x98, struct.unpack('<I', struct.pack('<f', vel[0]))[0])
    wf(w + 0xa0, struct.unpack('<I', struct.pack('<f', vel[1]))[0])
    wf(w + 0xa8, struct.unpack('<I', struct.pack('<f', vel[2]))[0])
    w32(fl + 0x94, flags)                             # [fl+0x94] = flags

    wf(b + 0x9c, wakeCounter)                         # +0x9c wakeCounter
    wf(b + 0x8c, body8c)                              # +0x8c (helper returns this as s0 if +0xb0==0)
    w64(b + 0xb0, 0)                                  # +0xb0 = 0 -> helper takes +0x80 path
    wf(b + 0x10, vel[0]); wf(b + 0x14, vel[1])
    wf(b + 0x18, vel[2]); wf(b + 0x1c, ang)

    wd(v + 0, 0.0); wf(v + 8, 0.0)
    wf(v + 0x10, vel[0]); wf(v + 0x14, vel[1]); wf(v + 0x18, vel[2])
    return mem, w, b, v


def run(wakeCounter=0.4, vel=(1.0, 0.0, 0.0), ang=0.0, dt=0.01,
        flags=0, body8c=1.0, max_steps=20000):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_LITTLE_ENDIAN)
    map_uc(uc, SO)
    mem, w, b, v = build_mem(wakeCounter, vel, ang, dt, flags, body8c)
    uc.mem_write(HEAP_BASE, bytes(mem))

    uc.reg_write(UC_ARM64_REG_SP, STACK_BASE + STACK_SIZE - 0x10)
    uc.reg_write(UC_ARM64_REG_X0, w)     # wrapper
    uc.reg_write(UC_ARM64_REG_X1, v)     # vel/transform arg
    uc.reg_write(UC_ARM64_REG_X2, 0)
    uc.reg_write(UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', dt))[0])

    trace_log = []
    stores = []

    def hook_code(uc, address, size, user_data):
        if address == FUNC:
            trace_log.append(('ENTER', hex(address)))
        if address == 0x13ce8f4:   # str s8, [x20, #0x9c] -> store wakeCounter
            wc = f32(uc.mem_read(b + 0x9c, 4))
            stores.append(('STORE_WAKECOUNTER', hex(address), wc))
        if address in (0x13ce734, 0x13ce73c, 0x13ce8b8):
            def s32(r):
                return f32(struct.pack('<I', uc.reg_read(r) & 0xffffffff))
            trace_log.append(('CMP', hex(address), 's3=%r s2=%r' % (s32(UC_ARM64_REG_S3), s32(UC_ARM64_REG_S2))))
        return True

    uc.hook_add(UC_HOOK_CODE, hook_code)

    try:
        uc.emu_start(FUNC, FUNC + 0x300, count=max_steps)
    except UcError as e:
        trace_log.append(('ERR', str(e)))

    wc = f32(uc.mem_read(b + 0x9c, 4))
    trace_log.append(('RESULT', 'wakeCounter=%r' % (wc,)))
    return trace_log, wc, stores


if __name__ == '__main__':
    print('=== Scenario A: wakeCounter=0.4 (init), vel=1.0 (fast), dt=0.01 ===')
    log, wc, stores = run(wakeCounter=0.4, vel=(1.0, 0.0, 0.0), dt=0.01)
    for e in log: print(e)
    for e in stores: print(e)
    print('final wakeCounter=%r\n' % wc)

    print('=== Scenario B: wakeCounter=0.0, vel=0.0 (stationary), dt=0.01 ===')
    log, wc, stores = run(wakeCounter=0.0, vel=(0.0, 0.0, 0.0), dt=0.01)
    for e in log: print(e)
    for e in stores: print(e)
    print('final wakeCounter=%r\n' % wc)

    print('=== Scenario C: wakeCounter=0.39 (<0.4, >=0.2), vel=0.0 (decay path), dt=0.01 ===')
    log, wc, stores = run(wakeCounter=0.39, vel=(0.0, 0.0, 0.0), dt=0.01)
    for e in log: print(e)
    for e in stores: print(e)
    print('final wakeCounter=%r\n' % wc)