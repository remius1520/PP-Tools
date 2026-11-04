import re,subprocess,sys,pickle,os
S=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SO=S+'/so/libil2cpp.so'; PK=S+'/rvamap.pkl'
if not os.path.exists(PK):
    m={}; pend=None; cls=None
    cx=re.compile(r'^(?:public |internal |private |protected )?(?:static |sealed |abstract |partial )*(?:class|struct|interface|enum) (\S+)')
    for line in open(S+'/dump/dump.cs',errors='ignore'):
        if line.startswith('\t// RVA:'):
            mm=re.match(r'^\t// RVA: 0x([0-9A-Fa-f]+)',line); pend=int(mm.group(1),16) if mm else None; continue
        if pend is not None and line.startswith('\t') and '(' in line:
            m.setdefault(pend,(cls,line.strip()[:120])); pend=None; continue
        if not line.startswith('\t') and line.strip() and not line.startswith(('//','{','}')):
            mm=cx.match(line)
            if mm: cls=mm.group(1)
    pickle.dump(m,open(PK,'wb'))
M=pickle.load(open(PK,'rb'))
def dis(rva,n=None):
    if n is None:
        nxt=min([k for k in M if k>rva] or [rva+0x800]); n=min(nxt-rva,0x3000)
    out=subprocess.run(['llvm-objdump','-d','--no-show-raw-insn','--triple=aarch64','--start-address=%#x'%rva,'--stop-address=%#x'%(rva+n),SO],capture_output=True,text=True).stdout
    res=[]
    for l in out.splitlines():
        mm=re.match(r'\s*([0-9a-f]+):\s+(.*)',l)
        if not mm: continue
        a=int(mm.group(1),16)
        t=mm.group(2)
        mt=re.search(r'\b(?:bl|b)\s+0x([0-9a-f]+)',t)
        if mt:
            tv=int(mt.group(1),16); r=M.get(tv)
            t+='   ; '+(('%s::%s'%(r[0],r[1])) if r else 'sub_%X'%tv)
        res.append('%X: %s'%(a,t))
    return '\n'.join(res)
if __name__=='__main__':
    print(dis(int(sys.argv[1],16), int(sys.argv[2],16) if len(sys.argv)>2 else None))
