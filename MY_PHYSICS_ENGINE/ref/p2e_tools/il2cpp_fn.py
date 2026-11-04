import re,sys,os
S=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.argv=sys.argv[:]
exec(open(S+'/tools/dis.py').read().split("if __name__")[0])
keys=sorted(M)
def size(r):
    n=[k for k in keys if k>r][0]; return n-r
def get(r):
    t=dis(r,size(r)); t=re.sub(r'\s*<[^>]*>','',t)
    return '\n'.join(l for l in t.splitlines() if not re.search(r'sub_3365(910|A8C)|sub_33659FC|sub_33658BC',l))
if __name__=='__main__': print(get(int(sys.argv[1],16)))
