# Decode the ServerTracker serialized tail from a level scene (160-byte MonoBehaviour, script PPtr (1,443)).  usage: st_tail.py <levelfile>
import struct,sys,re
d=open(sys.argv[1],'rb').read(); pat=struct.pack('<iq',1,443)
for m in re.finditer(re.escape(pat),d):
    s=m.start()-16; seg=d[s:s+160]
    if len(seg)<160: continue
    f=struct.unpack('<8f',seg[120:152]); i=struct.unpack('<2i',seg[152:160])
    if abs(f[6])<1e6 and abs(f[7])<1e6 and i[0]<1000 and f[6]>0:
        print('component @%d'%s,dict(zip('minXpoint maxXpoint minZpoint maxZpoint minXRack maxXRack substeps stopSimulateThreshold'.split(),f)),'solvecount',i[0],'isSolving',bool(i[1]&255))
