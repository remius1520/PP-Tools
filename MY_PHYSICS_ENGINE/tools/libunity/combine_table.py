# P2P: يطبع تحويل enum دمج الارتداد/الاحتكاك (Unity -> PxCombineMode) من libunity.so نفسه.
# الأمر 0x400000F (PhysX backend, type-0 table idx 15) => handler 0xc09174: v=int32 من ShotData material (+0x40/+0x44);
# v-1 (unsigned) <= 2 ? table[v-1] : 0 ; table = rodata 0x209e34 ; النتيجة -> PxMaterial vtable +0x88 (friction) / +0x98 (restitution).
import struct
LIB='/home/claude/ext/lib/arm64-v8a/libunity.so'
d=open(LIB,'rb').read()
VA=0x209e34   # rodata (va == file offset below 0x53f8dc)
tab=[struct.unpack('<I',d[VA+4*i:VA+4*i+4])[0] for i in range(3)]
PX={0:'eAVERAGE',1:'eMIN',2:'eMULTIPLY',3:'eMAX'}   # PxCombineMode (general knowledge, PhysX header)
UN={0:'Average',1:'Multiply',2:'Minimum',3:'Maximum'}
print('table @0x%x = %s'%(VA,tab))
for v in range(4):
    px = tab[v-1] if 1<=v<=3 else 0
    print('file value %d -> PxCombineMode %d (%s)'%(v,px,PX[px]))
