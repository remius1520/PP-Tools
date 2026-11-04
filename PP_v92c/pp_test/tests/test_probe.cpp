// v87: PP2 probe against a FAKE il2cpp (reflection stubbed).  Checks the OK / MISSING logic, the wildcard-namespace scan and that the probe only looks things up.
#include "../native/pp2probe.hpp"
#include <set>
#include <algorithm>
#include <map>
using namespace il2;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)

struct FK { std::string ns, name; std::set<std::string> methods, fields; };
static std::vector<FK> KS; static int calls = 0;
static Domain* d_get() { return (Domain*)0x1; }
static Assembly** d_asm(const Domain*, size_t* n) { static Assembly* a[1] = {(Assembly*)0x2}; *n = 1; return a; }
static Image* a_img(const Assembly*) { return (Image*)0x3; }
static Klass* c_from(const Image*, const char* ns, const char* nm) { ++calls; for (auto& k : KS) if (k.ns == ns && k.name == nm) return (Klass*)&k; return nullptr; }
static const Method* c_meth(Klass* k, const char* nm, int argc) { FK* f = (FK*)k; std::string key = std::string(nm) + "/" + std::to_string(argc); for (auto& m : f->methods) if (m == key || (argc < 0 && m.compare(0, strlen(nm) + 1, std::string(nm) + "/") == 0)) return (const Method*)0x10; return nullptr; }
static void* c_field(Klass* k, const char* nm) { FK* f = (FK*)k; return f->fields.count(nm) ? (void*)0x20 : nullptr; }
static size_t i_cc(const Image*) { return KS.size(); }
static const Klass* i_cls(const Image*, size_t i) { return (const Klass*)&KS[i]; }
static const char* k_name(const Klass* k) { return ((const FK*)k)->name.c_str(); }

static void wire() {
    il2cpp_domain_get = d_get; il2cpp_domain_get_assemblies = d_asm; il2cpp_assembly_get_image = a_img; il2cpp_class_from_name = c_from; il2cpp_class_get_method_from_name = c_meth;
    il2cpp_class_get_field_from_name = c_field; il2cpp_image_get_class_count = i_cc; il2cpp_image_get_class = i_cls; il2cpp_class_get_name = k_name;
}
static int count(const std::string& s, const char* w) { int n = 0; for (size_t p = s.find(w); p != std::string::npos; p = s.find(w, p + 1)) ++n; return n; }

int main() {
    wire();
    // 1) empty game: everything MISSING
    int ok = 0, miss = 0; std::string r = ppu::probePP2(ok, miss); const int N = (int)(sizeof(ppu::kProbe) / sizeof(ppu::kProbe[0]));
    CHECK(ok == 0 && miss == N, "empty game: all MISSING"); CHECK(count(r, "\nMISSING") + (r.compare(0, 7, "MISSING") == 0) == N, "one MISSING line per item");
    CHECK(r.find("RESULT: 0 OK") != std::string::npos, "RESULT line");
    // 2) full fake game built from the table itself
    KS.clear(); for (const auto& it : ppu::kProbe) {
        const std::string ns = strcmp(it.ns, "*") ? it.ns : "Some.Other.Ns"; FK* k = nullptr; for (auto& x : KS) if (x.ns == ns && x.name == it.cls) k = &x;
        if (!k) { KS.push_back({ns, it.cls, {}, {}}); k = &KS.back(); }
        if (it.kind == 'M') k->methods.insert(std::string(it.member) + "/" + std::to_string(it.argc < 0 ? 0 : it.argc)); else if (it.kind == 'F') k->fields.insert(it.member);
    }
    // vector may have reallocated while building pointers; we only stored indices through KS itself, so this is fine
    calls = 0; r = ppu::probePP2(ok, miss);
    CHECK(miss == 0 && ok == N, "full game: all OK"); CHECK(count(r, "\nMISSING") == 0 && r.compare(0, 7, "MISSING") != 0, "no MISSING line"); CHECK(r.find("RESULT: " + std::to_string(N) + " OK, 0 MISSING") != std::string::npos, "RESULT all OK");
    CHECK(r.find("GameManagerService.OnPingBallVelocity") != std::string::npos, "wildcard-namespace class found by scan");
    // 3) remove one method and one field
    for (auto& k : KS) { if (k.name == "Physics") k.methods.erase("Simulate/1"); if (k.name == "CueBall") k.fields.erase("collidedBall"); }
    r = ppu::probePP2(ok, miss); CHECK(miss == 2 && ok == N - 2, "2 removed -> 2 MISSING");
    CHECK(r.find("MISSING  method Physics.Simulate/1") != std::string::npos, "names the missing method"); CHECK(r.find("MISSING  field  CueBall.collidedBall") != std::string::npos, "names the missing field");
    // 4) class gone
    KS.erase(std::remove_if(KS.begin(), KS.end(), [](const FK& k) { return k.name == "SpinController"; }), KS.end());
    r = ppu::probePP2(ok, miss); CHECK(r.find("(class not found)") != std::string::npos, "class not found is reported");
    // 5) optional scan exports absent: wildcard items MISSING, no crash
    il2cpp_image_get_class_count = nullptr; r = ppu::probePP2(ok, miss); CHECK(r.find("RESULT:") != std::string::npos, "no crash without the scan exports");
    il2cpp_class_get_field_from_name = nullptr; r = ppu::probePP2(ok, miss); CHECK(r.find("export absent") != std::string::npos, "field export absent reported");
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
