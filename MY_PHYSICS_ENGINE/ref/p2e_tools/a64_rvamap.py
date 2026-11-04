import re,sys,pickle
cls=None; m={}
rx=re.compile(r'^\t// RVA: 0x([0-9A-Fa-f]+) Offset')
cx=re.compile(r'^(?:public |internal |private |protected )?(?:static |sealed |abstract |partial )*(?:class|struct|interface|enum) (\S+)')
pend=None
with open('dumpcs/dump.cs',errors='ignore') as f:
    for line in f:
        if line.startswith('\t// RVA:'):
            mm=rx.match(line)
            if mm: pend=int(mm.group(1),16)
            continue
        if pend is not None and line.startswith('\t') and '(' in line:
            m.setdefault(pend,(cls,line.strip()[:110])); pend=None
            continue
        if not line.startswith('\t') and not line.startswith('//') and not line.startswith('{') and not line.startswith('}') and line.strip():
            mm=cx.match(line)
            if mm: cls=mm.group(1)
pickle.dump(m,open('rvamap.pkl','wb')); print(len(m))
