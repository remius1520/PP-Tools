import struct,subprocess,re,sys
segs=[]
out=subprocess.run(['readelf','-lW','/home/claude/so/libil2cpp.so'],capture_output=True,text=True).stdout
for l in out.splitlines():
    p=l.split()
    if p and p[0]=='LOAD': segs.append((int(p[2],16),int(p[1],16),int(p[4],16)))  # vaddr, off, filesz
def off(va):
    for v,o,s in segs:
        if v<=va<v+s: return o+va-v
    raise Exception('unmapped %x'%va)
F=open('/home/claude/so/libil2cpp.so','rb')
def f32(va): F.seek(off(va)); return struct.unpack('<f',F.read(4))[0]
def f64(va): F.seek(off(va)); return struct.unpack('<d',F.read(8))[0]
def u32(va): F.seek(off(va)); return struct.unpack('<I',F.read(4))[0]
