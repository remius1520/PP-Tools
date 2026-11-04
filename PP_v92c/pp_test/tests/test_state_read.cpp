// PP v92c Part B1 test: state reading by NAME (mock il2cpp reflection).
#include "../native/state_read.hpp"
#include <cstdio>
#include <cstring>
using namespace il2;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)

    // ---- fake il2cpp reflection (same shape as test_unityhost.cpp) ----
struct Obj0 { void* klass = nullptr; void* mon = nullptr; void* cached = (void*)0x200000; };
struct FMethod { void* fn; const char* name; int argc; const char* first; };
struct FKlass : il2::Klass { const char* name; std::vector<FMethod*> ms; };
static std::vector<FKlass*> KL;
static FKlass* K(const char* n) { auto* k = new FKlass{n, {}}; KL.push_back(k); return k; }
static FMethod* mk(FKlass* k, const char* nm, void* fn, int argc, const char* first = nullptr) {
    auto* m = new FMethod{fn, nm, argc, first}; k->ms.push_back(m); return m;
}
static Domain* f_dom() { return (Domain*)0x1; }
static Assembly** f_asm(const Domain*, size_t* n) { static Assembly* a[1] = {(Assembly*)0x2}; *n = 1; return a; }
static Image* f_img(const Assembly*) { return (Image*)0x3; }
static Klass* f_cfn(const Image*, const char* ns, const char* nm) {
    for (auto* k : KL) if (!strcmp(k->name, nm)) return k; return nullptr;
}
static const Method* f_cgm(Klass* k, void** it) {
    auto* kk = (FKlass*)k; size_t i = (size_t)*it;
    if (i >= kk->ms.size()) return nullptr; *it = (void*)(i + 1);
    return (const Method*)kk->ms[i];
}
static const char* f_mn(const Method* m) { return ((const FMethod*)m)->name; }
static uint32_t f_pc(const Method* m) { return (uint32_t)((const FMethod*)m)->argc; }
static const Type* f_mp(const Method* m, uint32_t) { return (const Type*)((const FMethod*)m)->first; }
static bool f_gen(const Method*) { return false; }
static char* f_tn(const Type* t) { return (char*)t; }
static void* f_arr(Klass*, size_t n) { void* p = calloc(n + 4, 8); return p; }
static uint32_t f_gch(Obj*, bool) { return 1; }
static void* f_cfo(void*) { return nullptr; }   // il2cpp_field_get_offset (stubbed: name resolution fails -> offset fallback)
static void* f_cgffn(Klass*, const char*) { return nullptr; }  // il2cpp_class_get_field_from_name (stubbed: all name lookups fail)

int main() {
    il2cpp_domain_get = f_dom; il2cpp_domain_get_assemblies = f_asm;
    il2cpp_assembly_get_image = f_img; il2cpp_class_from_name = f_cfn;
    il2cpp_class_get_methods = f_cgm; il2cpp_method_get_name = f_mn;
    il2cpp_method_get_param_count = f_pc; il2cpp_method_get_param = f_mp;
    il2cpp_method_is_generic = f_gen; il2cpp_type_get_name = f_tn;
    il2cpp_array_new = f_arr; il2cpp_gchandle_new = f_gch;
    il2cpp_field_get_offset = f_cfo; il2cpp_class_get_field_from_name = f_cgffn;
    il2cpp_resolve_icall = nullptr;

    il2::Klass *kBall = K("Ball"), *kRb = K("Rigidbody"), *kGM = K("GameManager");
    il2::Klass *kCue = K("CueBall"), *kPb = K("PhysicsBuilder"), *kSpin = K("SpinController");

    // ballList with 2 items: cue(0) + ball(1)
    void* items = f_arr(nullptr, 2);
    *(int*)((char*)items + off::L_size) = 2;
    void* b0 = new Obj0(); void* b1 = new Obj0();
    *(void**)((char*)items + off::ARR_data) = b0; *(void**)((char*)items + off::ARR_data + 8) = b1;
    void* ballList = new Obj0();
    *(void**)((char*)ballList + off::L_items) = items;
    *(int*)((char*)ballList + off::L_size) = 2;

    // --- name resolution is STUBBED to fail (f_cgffn returns null) ---
    // => every field falls back to offset; method = "offset", ok = true
    ppu::StateReport rep;
    std::vector<ppu::BallStateEx> balls;
    int cue = ppu::readBallsByName(kBall, kRb, ballList, rep, balls);
    printf("%s", rep.str().c_str());
    CHECK(cue == 0, "cue ball is index 0");
    CHECK(balls.size() == 2, "two balls read");
    CHECK(balls[0].isCue, "ball 0 is cue");

    // every field reports method "offset" (name unavailable) but ok=true
    bool allOffset = true;
    for (auto& l : rep.str()) { /* N/A, check lines instead */ }
    // count lines containing "offset" method
    int offLines = 0; const char* p = rep.str().c_str();
    while (*p) { const char* nl = strchr(p, '\n'); size_t len = nl ? (size_t)(nl - p) : strlen(p);
        std::string line(p, len); if (line.find("offset") != std::string::npos) ++offLines;
        p = nl ? nl + 1 : p + len; }
    CHECK(offLines >= 4, "at least 4 fields report offset method");

    // shot state
    ppu::ShotStateEx ss; ppu::StateReport rep2;
    bool ok = ppu::readShotStateByName(kCue, kPb, kSpin, kGM, b0, nullptr, ballList, rep2, ss);
    printf("%s", rep2.str().c_str());
    CHECK(!ok, "spin not resolvable -> shot state not fully OK (name stubbed)");

    // RESULT line present
    CHECK(rep.str().find("RESULT:") != std::string::npos, "report has RESULT line");
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}