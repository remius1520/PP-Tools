# Scan an ARM64 range of libil2cpp.so for unsigned-offset ldr/str with given managed field offsets.  usage: fieldscan.py <lo_hex> <hi_hex> <off_hex>...
import struct,sys
SO=open('/home/claude/so/libil2cpp.so','rb')
lo=int(sys.argv[1],16);hi=int(sys.argv[2],16);offs=[int(x,16) for x in sys.argv[3:]]
SO.seek(lo-0x4000);data=SO.read(hi-lo)
for i in range(0,len(data),4):
    w=struct.unpack('<I',data[i:i+4])[0]
    pc=lo+i
    # unsigned-offset ldr/str: size bits 31:30, opc 23:22
    if (w&0x3b000000)==0x39000000:
        sz=w>>30; imm=(w>>10)&0xfff; off=imm<<sz
        if off in offs:
            rn=(w>>5)&31;rt=w&31;opc=(w>>22)&3
            print('%x: %s sz=%d rt=%d rn=x%d off=0x%x'%(pc,'ld' if opc&1 else 'st',1<<sz,rt,rn,off))
