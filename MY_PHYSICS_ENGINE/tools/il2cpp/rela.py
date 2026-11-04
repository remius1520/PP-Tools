import numpy as np,sys,struct
f=np.fromfile('/home/claude/so/libil2cpp.so',dtype=np.uint64,count=(0x314c0+0x12e35b0)//8)
r=f[0x314c0//8:].reshape(-1,3)   # r_offset, r_info, r_addend
off,info,add=r[:,0],r[:,1],r[:,2]
def slots_for_addend(a): return [hex(x) for x in off[add==a]]
if __name__=='__main__':
    for a in sys.argv[1:]:
        a=int(a,16); s=slots_for_addend(a); print('%x -> slots'%a,s)
        for x in s:
            print('   MethodInfo S=%s referenced by addend:'%x,slots_for_addend(int(x,16)))
