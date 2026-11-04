#!/usr/bin/env python3
"""Capture threshold computation at 0x13ce878/0x13ce8b8 with correct layout.

Hooks:
  0x13ce878: ldr w8, [x8, #0x94]  -> w8 = [flags_obj+0x94]
  0x13ce8b8: fcmp s2, s3          -> s2 = 0.5*KE, s3 = threshold
  0x13ce734: fcmp wakeCounter, 0.2f
"""
import struct
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


def build_mem(wakeCounter=0.1, vel=(1.0, 0.0, 0.0), dt=0.01, flags_at_94=0, body8c=1.0):
    w = HEAP_BASE + 0x0000
    b = HEAP_BASE + 0x1000
    v = HEAP_BASE + 0x2000
    fl = HEAP_BASE + 0x3000
    mem = bytearray(HEAP_SIZE)

    def w32(off, val): struct.pack_into('<I', mem, off - HEAP_BASE, val)
    def w64(off, val): struct.pack_into('<Q', mem, off - HEAP_BASE, val)
    def wf(off, val): struct.pack_into('<f', mem, off - HEAP_BASE, val)
    def wd(off, val): struct.pack_into('<d', mem, off - HEAP_BASE, val)

    w64(w + 0x50, b); w64(w + 0x88, fl); wd(w + 0x90, 0.0)
    wf(w + 0x98, struct.unpack('<I', struct.pack('<f', vel[0]))[0])
    wf(w + 0xa0, struct.unpack('<I', struct.pack('<f', vel[1]))[0])
    wf(w + 0xa8, struct.unpack('<I', struct.pack('<f', vel[2]))[0])
    w32(fl + 0x94, flags_at_94)
    wf(b + 0x9c, wakeCounter)
    wf(b + 0x8c, body8c)
    w64(b + 0xb0, 0)
    wf(b + 0x10, vel[0]); wf(b + 0x14, vel[1])
    wf(b + 0x18, vel[2]); wf(b + 0x1c, 0.0)
    wd(v + 0, 0.0); wf(v + 8, 0.0)
    wf(v + 0x10, vel[0]); wf(v + 0x14, vel[1]); wf(v + 0x18, vel[2])
    return mem, w, b, v


def run(wakeCounter=0.1, vel=(1.0, 0.0, 0.0), dt=0.01, flags_at_94=0, body8c=1.0, max_steps=80000):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_LITTLE_ENDIAN)
    map_uc(uc, SO)
    mem, w, b, v = build_mem(wakeCounter, vel, dt, flags_at_94, body8c)
    uc.mem_write(HEAP_BASE, bytes(mem))
    uc.reg_write(UC_ARM64_REG_SP, STACK_BASE + STACK_SIZE - 0x10)
    uc.reg_write(UC_ARM64_REG_X0, w)
    uc.reg_write(UC_ARM64_REG_X1, v)
    uc.reg_write(UC_ARM64_REG_X2, 0)
    uc.reg_write(UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', dt))[0])

    events = []

    def hook_code(uc, address, size, user_data):
        def s32(r):
            return f32(struct.pack('<I', uc.reg_read(r) & 0xffffffff))
        if address == 0x13ce878:
            w8 = uc.reg_read(UC_ARM64_REG_W8)
            events.append(('W8_94', hex(address), '[flags+0x94]=%d(0x%x)' % (w8 & 0xffffffff, w8 & 0xffffffff)))
        if address == 0x13ce8b8:
            events.append(('KECMP', hex(address),
                           's2(0.5KE)=%r s3(threshold)=%r' % (s32(UC_ARM64_REG_S2), s32(UC_ARM64_REG_S3))))
        if address == 0x13ce734:
            events.append(('WCCMP', hex(address), 'wakeCounter=%r vs 0.2' % s32(UC_ARM64_REG_S3)))
        if address == 0x13ce73c:
            events.append(('WCDT', hex(address), 'wakeCounter=%r vs s0(dt)=%r' % (s32(UC_ARM64_REG_S3), s32(UC_ARM64_REG_S0))))
        return True

    uc.hook_add(UC_HOOK_CODE, hook_code)
    try:
        uc.emu_start(FUNC, FUNC + 0x300, count=max_steps)
    except UcError as e:
        events.append(('ERR', str(e)))
    wc = f32(uc.mem_read(b + 0x9c, 4))
    events.append(('RESULT', 'wakeCounter=%r' % wc))
    return events, wc


if __name__ == '__main__':
    # Test with flags_at_94 = 0 -> threshold should be (0+1)*dt = 0.01
    print('=== flags_at_94=0, WC=0.1 (<0.2), vel=0.0 (KE=0) ===')
    events, wc = run(wakeCounter=0.1, vel=(0.0, 0.0, 0.0), flags_at_94=0)
    for e in events:
        print('  ', e)
    print()
    print('=== flags_at_94=0, WC=0.4 (>=0.2), vel=0.0 (KE=0) ===')
    events, wc = run(wakeCounter=0.4, vel=(0.0, 0.0, 0.0), flags_at_94=0)
    for e in events:
        print('  ', e)
    print()
    print('=== flags_at_94=9, WC=0.1 (<0.2), vel=0.0 (KE=0) -> threshold=(9+1)*0.01=0.1 ===')
    events, wc = run(wakeCounter=0.1, vel=(0.0, 0.0, 0.0), flags_at_94=9)
    for e in events:
        print('  ', e)