import re,sys,subprocess,bisect,pickle,os
D=os.environ.get('PP2_WORK','.')+'/'   # dir holding dump/dump.cs and il/libil2cpp.so; usage: PP2_WORK=dir python3 il2cpp_fn2.py <RVA hex>
if not os.path.exists(D+'rva.pkl'):
    M={};cls=None
    for l in open(D+'dump/dump.cs',errors='ignore'):
        m=re.match(r'(?:public |internal |private |protected )?(?:sealed |static |abstract )*(?:class|struct|enum|interface) (\S+)',l)
        if m and not l.startswith('\t'): cls=m.group(1)
        m=re.match(r'\t// RVA: 0x([0-9A-F]+)',l)
        if m: pend=int(m.group(1),16); continue
        if 'pend' in dir() and pend and l.startswith('\t') and '{ }' in l:
            M[pend]=(cls or '?')+'::'+re.sub(r'\s+',' ',l.strip().split('{')[0]); pend=None
    pickle.dump(M,open(D+'rva.pkl','wb'))
M=pickle.load(open(D+'rva.pkl','rb')); K=sorted(M)
def name(a):
    return M.get(a)
def dis(a):
    i=bisect.bisect_right(K,a); end=K[i] if i<len(K) else a+0x800
    out=subprocess.run(['llvm-objdump','-d','--no-show-raw-insn',f'--start-address={a}',f'--stop-address={end}',D+'il/libil2cpp.so'],capture_output=True,text=True).stdout
    res=[]
    for l in out.splitlines():
        if not re.match(r'\s+[0-9a-f]+:',l): continue
        l=re.sub(r'<[^>]*>','',l)
        m=re.search(r'\bbl?\s+0x([0-9a-f]+)',l)
        if m:
            n=name(int(m.group(1),16))
            if n: l+='   ; '+n
        res.append(l)
    return '\n'.join(res),end-a
if __name__=='__main__':
    t,n=dis(int(sys.argv[1],16)); print(f'; {name(int(sys.argv[1],16))} size={n:#x}'); print(t)
