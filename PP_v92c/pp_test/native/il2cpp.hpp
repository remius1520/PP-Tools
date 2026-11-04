// Minimal il2cpp reflection layer. Everything is resolved BY NAME at runtime (no hard-coded RVAs),
// so it survives game updates as long as the (unobfuscated) names/field offsets in the dump still hold.
#pragma once
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <cstring>
#include <cstdint>
#include <cstddef>

namespace il2 {

struct Domain; struct Assembly; struct Image; struct Klass; struct Method; struct Type; struct Obj;

#define IL2_FN(ret, name, args) static ret (*name) args = nullptr;
IL2_FN(Domain*, il2cpp_domain_get, ())
IL2_FN(Assembly**, il2cpp_domain_get_assemblies, (const Domain*, size_t*))
IL2_FN(Image*, il2cpp_assembly_get_image, (const Assembly*))
IL2_FN(const char*, il2cpp_image_get_name, (const Image*))
IL2_FN(Klass*, il2cpp_class_from_name, (const Image*, const char*, const char*))
IL2_FN(const Method*, il2cpp_class_get_method_from_name, (Klass*, const char*, int))
IL2_FN(const Method*, il2cpp_class_get_methods, (Klass*, void**))
IL2_FN(const char*, il2cpp_method_get_name, (const Method*))
IL2_FN(uint32_t, il2cpp_method_get_param_count, (const Method*))
IL2_FN(const Type*, il2cpp_method_get_param, (const Method*, uint32_t))
IL2_FN(bool, il2cpp_method_is_generic, (const Method*))
IL2_FN(char*, il2cpp_type_get_name, (const Type*))
IL2_FN(const Type*, il2cpp_class_get_type, (Klass*))
IL2_FN(Obj*, il2cpp_type_get_object, (const Type*))
IL2_FN(void*, il2cpp_thread_attach, (Domain*))
IL2_FN(uint32_t, il2cpp_gchandle_new, (Obj*, bool))        // v21: strong GC handle - keeps a managed object alive while only native code points at it
IL2_FN(size_t, il2cpp_image_get_class_count, (const Image*))   // v26 optional: used only by the one-press capture to find methods by name in every class
IL2_FN(const Klass*, il2cpp_image_get_class, (const Image*, size_t))
IL2_FN(const char*, il2cpp_class_get_name, (const Klass*))
IL2_FN(const char*, il2cpp_class_get_namespace, (const Klass*))
IL2_FN(void, il2cpp_gchandle_free, (uint32_t))
IL2_FN(void*, il2cpp_resolve_icall, (const char*))          // v37f optional: Unity internal calls still exist when the managed wrapper was stripped
IL2_FN(void*, il2cpp_class_get_fields, (Klass*, void**))          // v37e optional: list fields (query file "fields")
IL2_FN(const char*, il2cpp_field_get_name, (void*))
IL2_FN(size_t, il2cpp_field_get_offset, (void*))
IL2_FN(void*, il2cpp_class_get_field_from_name, (Klass*, const char*))   // v37 optional: read the static restitution floats of PhysicsSolver
IL2_FN(void, il2cpp_field_static_get_value, (void*, void*))     // optional: a foreign thread must be attached before it touches metadata / allocates managed objects
IL2_FN(void*, il2cpp_array_new, (Klass*, size_t))               // v84: Collider[] buffer for Physics.OverlapSphereNonAlloc (live predictor)

// ---- v10: find libil2cpp.so WITHOUT dlopen. dlopen("libil2cpp.so") from a LD_PRELOADed lib looks in OUR linker namespace, but the game loaded
// il2cpp in the app's classloader namespace, so it was never found ("[2] libil2cpp.so never appeared"). dl_iterate_phdr sees every namespace,
// and we read the module's own dynamic symbol table to resolve the il2cpp_* exports.
struct Mod { uintptr_t base = 0; const ElfW(Sym)* sym = nullptr; const char* str = nullptr; size_t nsym = 0; char path[256] = {0}; };

inline bool findModule(const char* needle, Mod& out) {
    struct Ctx { const char* needle; Mod* out; bool ok; } ctx{needle, &out, false};
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* d) -> int {
        Ctx* c = (Ctx*)d;
        if (!info->dlpi_name || !strstr(info->dlpi_name, c->needle)) return 0;
        const ElfW(Dyn)* dyn = nullptr;
        for (int i = 0; i < info->dlpi_phnum; ++i) if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        if (!dyn) return 0;
        uintptr_t symtab = 0, strtab = 0, gnu = 0, hash = 0;
        auto fix = [&](uintptr_t p) { return p < info->dlpi_addr ? p + info->dlpi_addr : p; };   // some loaders relocate d_ptr in place, some do not
        for (const ElfW(Dyn)* x = dyn; x->d_tag != DT_NULL; ++x) {
            if (x->d_tag == DT_SYMTAB) symtab = fix(x->d_un.d_ptr);
            else if (x->d_tag == DT_STRTAB) strtab = fix(x->d_un.d_ptr);
            else if (x->d_tag == DT_GNU_HASH) gnu = fix(x->d_un.d_ptr);
            else if (x->d_tag == DT_HASH) hash = fix(x->d_un.d_ptr);
        }
        if (!symtab || !strtab) return 0;
        size_t n = 0;
        if (gnu) {
            const uint32_t* h = (const uint32_t*)gnu; uint32_t nb = h[0], so = h[1], bs = h[2];
            const uint32_t* buckets = h + 4 + bs * (sizeof(ElfW(Addr)) / 4); const uint32_t* chain = buckets + nb;
            uint32_t mx = 0; for (uint32_t i = 0; i < nb; ++i) if (buckets[i] > mx) mx = buckets[i];
            if (mx < so) n = so; else { uint32_t i = mx; while (!(chain[i - so] & 1)) ++i; n = (size_t)i + 1; }
        } else if (hash) n = ((const uint32_t*)hash)[1];
        if (!n) return 0;
        c->out->base = info->dlpi_addr; c->out->sym = (const ElfW(Sym)*)symtab; c->out->str = (const char*)strtab; c->out->nsym = n;
        strncpy(c->out->path, info->dlpi_name, sizeof(c->out->path) - 1);
        c->ok = true; return 1;
    }, &ctx);
    return ctx.ok;
}

inline void* modSym(const Mod& m, const char* name) {
    for (size_t i = 0; i < m.nsym; ++i) {
        const ElfW(Sym)& s = m.sym[i];
        if (s.st_shndx == SHN_UNDEF || !s.st_name) continue;
        if (!strcmp(m.str + s.st_name, name)) return (void*)(m.base + s.st_value);
    }
    return nullptr;
}

inline bool loadMod(const Mod& m) {
#define LDM(n) n = (decltype(n))modSym(m, #n); if (!n) return false;
    LDM(il2cpp_domain_get) LDM(il2cpp_domain_get_assemblies) LDM(il2cpp_assembly_get_image) LDM(il2cpp_image_get_name)
    LDM(il2cpp_class_from_name) LDM(il2cpp_class_get_method_from_name) LDM(il2cpp_class_get_methods) LDM(il2cpp_method_get_name)
    LDM(il2cpp_method_get_param_count) LDM(il2cpp_method_get_param) LDM(il2cpp_method_is_generic) LDM(il2cpp_type_get_name)
    LDM(il2cpp_class_get_type) LDM(il2cpp_type_get_object)
#undef LDM
    il2cpp_thread_attach = (decltype(il2cpp_thread_attach))modSym(m, "il2cpp_thread_attach");
    il2cpp_gchandle_new = (decltype(il2cpp_gchandle_new))modSym(m, "il2cpp_gchandle_new"); il2cpp_gchandle_free = (decltype(il2cpp_gchandle_free))modSym(m, "il2cpp_gchandle_free");
    il2cpp_image_get_class_count = (decltype(il2cpp_image_get_class_count))modSym(m, "il2cpp_image_get_class_count"); il2cpp_image_get_class = (decltype(il2cpp_image_get_class))modSym(m, "il2cpp_image_get_class");
    il2cpp_class_get_name = (decltype(il2cpp_class_get_name))modSym(m, "il2cpp_class_get_name"); il2cpp_class_get_namespace = (decltype(il2cpp_class_get_namespace))modSym(m, "il2cpp_class_get_namespace");
    // v69d: loadMod() (the path the game actually takes: dlopen fails inside the app namespace, so dl_iterate_phdr + .dynsym is used) never looked up these four optional exports,
    // only load() did -> "resolveApi=0" in pp_icalls.txt although libil2cpp exports il2cpp_resolve_icall. Same lookup as in load().
    il2cpp_resolve_icall = (decltype(il2cpp_resolve_icall))modSym(m, "il2cpp_resolve_icall");
    il2cpp_class_get_fields = (decltype(il2cpp_class_get_fields))modSym(m, "il2cpp_class_get_fields"); il2cpp_field_get_name = (decltype(il2cpp_field_get_name))modSym(m, "il2cpp_field_get_name"); il2cpp_field_get_offset = (decltype(il2cpp_field_get_offset))modSym(m, "il2cpp_field_get_offset");
    il2cpp_class_get_field_from_name = (decltype(il2cpp_class_get_field_from_name))modSym(m, "il2cpp_class_get_field_from_name"); il2cpp_field_static_get_value = (decltype(il2cpp_field_static_get_value))modSym(m, "il2cpp_field_static_get_value");
    il2cpp_array_new = (decltype(il2cpp_array_new))modSym(m, "il2cpp_array_new");
    return true;
}

inline bool load(void* h) {
#define LD(n) n = (decltype(n))dlsym(h, #n); if (!n) return false;
    LD(il2cpp_domain_get) LD(il2cpp_domain_get_assemblies) LD(il2cpp_assembly_get_image) LD(il2cpp_image_get_name)
    LD(il2cpp_class_from_name) LD(il2cpp_class_get_method_from_name) LD(il2cpp_class_get_methods) LD(il2cpp_method_get_name)
    LD(il2cpp_method_get_param_count) LD(il2cpp_method_get_param) LD(il2cpp_method_is_generic) LD(il2cpp_type_get_name)
    LD(il2cpp_class_get_type) LD(il2cpp_type_get_object)
#undef LD
    il2cpp_thread_attach = (decltype(il2cpp_thread_attach))dlsym(h, "il2cpp_thread_attach");
    il2cpp_gchandle_new = (decltype(il2cpp_gchandle_new))dlsym(h, "il2cpp_gchandle_new"); il2cpp_gchandle_free = (decltype(il2cpp_gchandle_free))dlsym(h, "il2cpp_gchandle_free");
    il2cpp_image_get_class_count = (decltype(il2cpp_image_get_class_count))dlsym(h, "il2cpp_image_get_class_count"); il2cpp_image_get_class = (decltype(il2cpp_image_get_class))dlsym(h, "il2cpp_image_get_class");
    il2cpp_class_get_name = (decltype(il2cpp_class_get_name))dlsym(h, "il2cpp_class_get_name"); il2cpp_class_get_namespace = (decltype(il2cpp_class_get_namespace))dlsym(h, "il2cpp_class_get_namespace");
    il2cpp_resolve_icall = (decltype(il2cpp_resolve_icall))dlsym(h, "il2cpp_resolve_icall");
    il2cpp_class_get_fields = (decltype(il2cpp_class_get_fields))dlsym(h, "il2cpp_class_get_fields"); il2cpp_field_get_name = (decltype(il2cpp_field_get_name))dlsym(h, "il2cpp_field_get_name"); il2cpp_field_get_offset = (decltype(il2cpp_field_get_offset))dlsym(h, "il2cpp_field_get_offset");
    il2cpp_class_get_field_from_name = (decltype(il2cpp_class_get_field_from_name))dlsym(h, "il2cpp_class_get_field_from_name"); il2cpp_field_static_get_value = (decltype(il2cpp_field_static_get_value))dlsym(h, "il2cpp_field_static_get_value");
    il2cpp_array_new = (decltype(il2cpp_array_new))dlsym(h, "il2cpp_array_new");
    return true;
}

inline Klass* findClass(const char* ns, const char* name) {
    size_t n = 0; Assembly** as = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; ++i) { Klass* k = il2cpp_class_from_name(il2cpp_assembly_get_image(as[i]), ns, name); if (k) return k; }
    return nullptr;
}

// MethodInfo layout: methodPointer is the first field (stable across il2cpp versions we care about).
inline void*& fnptr(const Method* m) { return *(void**)(void*)m; }

// first NON-generic method with this name/arg-count whose first param type name matches (nullptr = any)
inline const Method* findMethod(Klass* k, const char* name, int argc, const char* firstParam = nullptr) {
    void* it = nullptr;
    while (const Method* m = il2cpp_class_get_methods(k, &it)) {
        if (strcmp(il2cpp_method_get_name(m), name) || (int)il2cpp_method_get_param_count(m) != argc || il2cpp_method_is_generic(m)) continue;
        if (firstParam && argc > 0) { const char* tn = il2cpp_type_get_name(il2cpp_method_get_param(m, 0)); if (!tn || strcmp(tn, firstParam)) continue; }
        return m;
    }
    return nullptr;
}

} // namespace il2
