import struct, sys, io

class R:
    def __init__(s, b, o=0, big=False): s.b=b; s.p=o; s.e='>' if big else '<'
    def u8(s): v=s.b[s.p]; s.p+=1; return v
    def i8(s): v=struct.unpack_from('b',s.b,s.p)[0]; s.p+=1; return v
    def u16(s): v=struct.unpack_from(s.e+'H',s.b,s.p)[0]; s.p+=2; return v
    def i16(s): v=struct.unpack_from(s.e+'h',s.b,s.p)[0]; s.p+=2; return v
    def u32(s): v=struct.unpack_from(s.e+'I',s.b,s.p)[0]; s.p+=4; return v
    def i32(s): v=struct.unpack_from(s.e+'i',s.b,s.p)[0]; s.p+=4; return v
    def u64(s): v=struct.unpack_from(s.e+'Q',s.b,s.p)[0]; s.p+=8; return v
    def i64(s): v=struct.unpack_from(s.e+'q',s.b,s.p)[0]; s.p+=8; return v
    def f32(s): v=struct.unpack_from(s.e+'f',s.b,s.p)[0]; s.p+=4; return v
    def f64(s): v=struct.unpack_from(s.e+'d',s.b,s.p)[0]; s.p+=8; return v
    def cstr(s):
        e=s.b.index(b'\0',s.p); v=s.b[s.p:e].decode('utf8','replace'); s.p=e+1; return v
    def align(s,n=4): s.p=(s.p+n-1)//n*n
    def bytes(s,n): v=s.b[s.p:s.p+n]; s.p+=n; return v

CS = None  # common strings
COMMON = ("AABB\0AnimationClip\0AnimationCurve\0AnimationState\0Array\0Base\0BitField\0bitset\0bool\0char\0ColorRGBA\0Component\0data\0deque\0double\0dynamic_array\0FastPropertyName\0first\0float\0Font\0GameObject\0Generic Mono\0GradientNEW\0GUID\0GUIStyle\0int\0list\0long long\0map\0Matrix4x4f\0MdFour\0MonoBehaviour\0MonoScript\0m_ByteSize\0m_Curve\0m_EditorClassIdentifier\0m_EditorHideFlags\0m_Enabled\0m_ExtensionPtr\0m_GameObject\0m_Index\0m_IsArray\0m_IsStatic\0m_MetaFlag\0m_Name\0m_ObjectHideFlags\0m_PrefabInternal\0m_PrefabParentObject\0m_Script\0m_StaticEditorFlags\0m_Type\0m_Version\0Object\0pair\0PPtr<Component>\0PPtr<GameObject>\0PPtr<Material>\0PPtr<MonoBehaviour>\0PPtr<MonoScript>\0PPtr<Object>\0PPtr<Prefab>\0PPtr<Sprite>\0PPtr<TextAsset>\0PPtr<Texture>\0PPtr<Texture2D>\0PPtr<Transform>\0Prefab\0Quaternionf\0Rectf\0RectInt\0RectOffset\0second\0set\0short\0size\0SInt16\0SInt32\0SInt64\0SInt8\0staticvector\0string\0TextAsset\0TextMesh\0Texture\0Texture2D\0Transform\0TypelessData\0UInt16\0UInt32\0UInt64\0UInt8\0unsigned int\0unsigned long long\0unsigned short\0vector\0Vector2f\0Vector3f\0Vector4f\0m_ScriptingClassIdentifier\0Gradient\0Type*\0int2_storage\0int3_storage\0BoundsInt\0m_CorrespondingSourceObject\0m_PrefabInstance\0m_PrefabAsset\0FileSize\0Hash128\0RenderingLayerMask\0")
def common_str(off):
    e=COMMON.index('\0',off); return COMMON[off:e]

class Node: 
    __slots__=('ver','lvl','type','name','size','idx','flags','meta')

def read_tree(r, ver):
    n=r.u32(); sl=r.u32()
    recs=[]
    for _ in range(n):
        v=r.u16(); lvl=r.u8(); fl=r.u8(); to=r.u32(); no=r.u32(); sz=r.i32(); ix=r.u32(); mf=r.u32()
        if ver>=19: r.u64()
        recs.append((v,lvl,fl,to,no,sz,ix,mf))
    sb=r.bytes(sl)
    def gs(o):
        if o&0x80000000: return common_str(o&0x7fffffff)
        e=sb.index(b'\0',o); return sb[o:e].decode()
    nodes=[]
    for v,lvl,fl,to,no,sz,ix,mf in recs:
        nd=Node(); nd.ver=v; nd.lvl=lvl; nd.flags=fl; nd.type=gs(to); nd.name=gs(no); nd.size=sz; nd.idx=ix; nd.meta=mf; nodes.append(nd)
    return nodes

class Obj:
    pass

class SF:
    def __init__(s, data):
        s.data=data
        r=R(data,0,True)
        s.meta_size=r.u32(); s.file_size=r.u32(); s.version=r.u32(); s.data_off=r.u32()
        endian=r.u8(); r.bytes(3)
        if s.version>=22:
            s.meta_size=r.u32(); s.file_size=r.u64(); s.data_off=r.u64(); r.u64()
        r.e='<' if endian==0 else '>'
        s.unity=r.cstr(); s.platform=r.u32()
        s.has_tt=bool(r.u8())
        nt=r.u32()
        s.types=[]
        for _ in range(nt):
            cid=r.i32()
            stripped=r.u8(); sidx=r.i16()
            if cid==114: sh=r.bytes(16)
            else: sh=None
            sh2=r.bytes(16)
            tree=None
            if s.has_tt: tree=read_tree(r,s.version)
            deps=None
            if s.has_tt and s.version>=21:
                nd=r.u32(); r.bytes(4*nd)
            s.types.append(dict(cid=cid,stripped=stripped,sidx=sidx,script=sh,hash=sh2,tree=tree))
        if s.version>=21:
            # ref type offset flag? In v21+, "IsRefType" read before; handled below
            pass
        nobj=r.u32()
        s.objs=[]
        for _ in range(nobj):
            if s.version>=14: r.align(4)
            pid=r.i64() if s.version>=14 else r.i32()
            off=r.u64() if s.version>=22 else r.u32()
            sz=r.u32(); tid=r.i32()
            o=Obj(); o.pid=pid; o.off=off+s.data_off; o.size=sz; o.tid=tid
            t=s.types[tid]; o.cid=t['cid']; o.type=t
            s.objs.append(o)
        s.r=r

if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    f=SF(d)
    print(f.unity,f.version,f.has_tt,len(f.types),len(f.objs))
    from collections import Counter
    print(Counter(o.cid for o in f.objs).most_common(30))
