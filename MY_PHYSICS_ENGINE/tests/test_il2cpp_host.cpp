// P2Z: compile + fail-closed checks of android/pp2_il2cpp_host.cpp against a STUB il2cpp API (no game, no PhysX).  Real behaviour needs a device.
#include "../android/pp2_il2cpp_host.hpp"
#include <cstdio>
using namespace pp2::il2;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s\n", msg); } } while (0)
static char g_dummy[256];
static Api stubApi(bool resolveAll) {
    Api a; a.domain_get = [] () -> void* { return g_dummy; }; a.domain_assembly_open = [] (void*, const char*) -> void* { return g_dummy; }; a.assembly_get_image = [] (void*) -> void* { return g_dummy; };
    static bool all; all = resolveAll;
    a.class_from_name = [] (void*, const char*, const char*) -> void* { return all ? (void*)g_dummy : nullptr; };
    a.class_get_method_from_name = [] (void*, const char*, int) -> const void* { return all ? (const void*)g_dummy : nullptr; };
    a.class_get_field_from_name = [] (void*, const char*) -> void* { return all ? (void*)g_dummy : nullptr; };
    a.field_get_value = [] (void*, void*, void*) {}; a.runtime_invoke = [] (const void*, void*, void**, void**) -> void* { return nullptr; };
    a.object_unbox = [] (void* o) -> void* { return o; }; a.object_get_class = [] (void*) -> void* { return g_dummy; }; a.class_get_name = [] (void*) -> const char* { return "X"; };
    a.array_new = [] (void*, uintptr_t) -> void* { return g_dummy; }; a.string_new = [] (const char*) -> void* { return g_dummy; }; a.array_object_header_size = [] () -> size_t { return 0x20; };
    return a;
}
int main() {
    { Api a = stubApi(false); GameHost h(a); std::string rep; CHECK(!h.init(rep), "missing classes -> init false"); CHECK(rep.find("MISSING class") != std::string::npos, "report names the missing class"); std::string why; CHECK(!h.refresh(why), "refresh refuses when not initialised"); }
    { Api a = stubApi(true); GameHost h(a); std::string rep; CHECK(h.init(rep), "all names resolve -> init true"); CHECK(rep.find("MISSING") == std::string::npos, "no MISSING line");
      std::string why; CHECK(!h.refresh(why) && !why.empty(), "refresh fails closed when Physics.simulationMode cannot be read"); }
    { Api a; GameHost h(a); std::string rep; CHECK(!h.init(rep), "incomplete api table rejected"); }
    {   // P2Z4: formatting / broken-read detection (pure functions) + fail-closed sampling
        GameHost::TickRec r; r.fixedTime = 12.34; r.mode = 0; r.hit = 1; GameHost::TickBall a; a.ok = true; a.pos = {0.5, -0.0296, -0.25}; a.vel = {0.0004, 0, -1.5}; a.kin = 0; a.damp = 0.62; a.sleeping = 0; r.b.push_back(a);
        GameHost::TickBall bad; bad.ok = false; r.b.push_back(bad);
        const std::string s = GameHost::formatTick(r, 7);
        CHECK(s == "#7 T=12.340000 mode=0 hit=1 0:0.500000,-0.029600,-0.250000|0.000400,0.000000,-1.500000|0|0.6200|0 1:ERR", "formatTick exact line (6 decimals keep v<0.002 visible)");
        CHECK(!GameHost::looksBroken(r), "moving ball -> not broken");
        GameHost::TickRec z; GameHost::TickBall zb; zb.ok = true; z.b.push_back(zb); CHECK(GameHost::looksBroken(z), "all-zero pos+vel -> flagged as broken read");
        CHECK(GameHost::looksBroken(GameHost::TickRec()), "empty record -> broken");
        Api a2 = stubApi(true); GameHost h(a2); std::string rep, why; h.init(rep); GameHost::TickRec out; CHECK(!h.sampleTick(out, why) && !why.empty(), "sampleTick fails closed on a null game");
    }
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
