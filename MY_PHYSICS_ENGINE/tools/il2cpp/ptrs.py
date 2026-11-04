import numpy as np,sys,struct
d=np.fromfile('/home/claude/so/libil2cpp.so',dtype=np.uint64)
tg=[int(x,16) for x in sys.argv[1:]]
for x in tg:
    # il2cpp pointers are plain VA (rva) in non-relocated file or relocated; search both x and x+0x0
    hits=np.nonzero(d==x)[0]
    print('%x: u64 hits'%x,[hex(h*8) for h in hits[:10]])
# tail-call b
d32=np.fromfile('/home/claude/so/libil2cpp.so',dtype=np.uint32)
idx=np.nonzero((d32>>26)==0b000101)[0]
w=d32[idx].astype(np.int64)&0x3ffffff
w=np.where(w>>25&1,w-(1<<26),w)
pc=idx*4+0x4000; t=pc+w*4
for x in tg: print('%x tailb:'%x,[hex(p) for p in pc[t==x]])
