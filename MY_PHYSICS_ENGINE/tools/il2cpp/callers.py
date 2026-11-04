import numpy as np,sys,pickle
M=pickle.load(open('/home/claude/rvamap.pkl','rb'))
d=np.fromfile('/home/claude/so/libil2cpp.so',dtype=np.uint32)  # file offset = rva-0x4000 for text
tg=[int(x,16) for x in sys.argv[1:]]
idx=np.nonzero((d>>26)==0b100101)[0]
w=d[idx].astype(np.int64)&0x3ffffff
w=np.where(w>>25&1,w-(1<<26),w)
pc=idx*4+0x4000
t=pc+w*4
fs=sorted(M)
import bisect
for x in tg:
    print('== callers of %x'%x)
    for p in pc[t==x]:
        i=bisect.bisect_right(fs,int(p))-1; f=fs[i] if i>=0 else 0
        print('  %x in %x %s'%(p,f,M.get(f,('?','?'))))
