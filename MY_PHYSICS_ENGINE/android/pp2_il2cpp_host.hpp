// P2Z -- binds live::Host to the running game through the il2cpp C API (exported by libil2cpp.so: il2cpp_class_from_name, il2cpp_runtime_invoke, ...).
// Everything is resolved BY NAME (class / method / field), so no RVA depends on this build.  Names come from dump.cs (checked in P2Z).
// init() is a dry probe: it only resolves names and reads one flag; it never calls Physics.Simulate nor changes any game state.
// MUST run on the Unity main thread.  NOT run on a device yet (host-compiled against a stub API only: tests/test_il2cpp_host.cpp).
#pragma once
#include "../src/pp2_live.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace pp2 { namespace il2 {

struct Api {   // function pointers; fill with dlsym(libil2cpp handle, "il2cpp_...") (see loadApi in pp2_il2cpp_host.cpp)
    void* (*domain_get)() = nullptr; void* (*domain_assembly_open)(void*, const char*) = nullptr; void* (*assembly_get_image)(void*) = nullptr;
    void* (*class_from_name)(void*, const char*, const char*) = nullptr; const void* (*class_get_method_from_name)(void*, const char*, int) = nullptr;
    void* (*class_get_field_from_name)(void*, const char*) = nullptr; void (*field_get_value)(void*, void*, void*) = nullptr;
    void (*field_static_get_value)(void*, void*) = nullptr;
    void* (*runtime_invoke)(const void*, void*, void**, void**) = nullptr; void* (*object_unbox)(void*) = nullptr; void* (*object_get_class)(void*) = nullptr;
    const char* (*class_get_name)(void*) = nullptr; void* (*array_new)(void*, uintptr_t) = nullptr; void* (*string_new)(const char*) = nullptr;
    void* (*thread_attach)(void*) = nullptr; size_t (*array_object_header_size)() = nullptr; void* (*string_chars_dummy)() = nullptr;
    bool complete() const { return domain_get && domain_assembly_open && assembly_get_image && class_from_name && class_get_method_from_name && class_get_field_from_name && field_get_value && runtime_invoke && object_unbox && object_get_class && class_get_name && array_new && string_new && array_object_header_size; }
};
bool loadApi(void* dlHandle, Api& out, std::string& err);   // android only (dlsym); stub builds fill Api by hand

class GameHost : public live::Host {
public:
    explicit GameHost(const Api& a) : A(a) {}
    // Dry probe: resolves every class/method/field the live predictor needs.  report = one line per item ("OK"/"MISSING ...").  false if anything required is missing.
    bool init(std::string& report);
    // Reads GameManager.Balls (ids = list index; cueId = index of the CueBall).  Call right before predict() (balls may respawn between frames).  Refuses when unsafe
    // (Physics.simulationMode != Script, GameManager.isHit == true, no cue ball).
    bool refresh(std::string& why, bool forLog = false);   // forLog=true (P2Z4): skip the Script-mode / isHit refusals -- read-only, used by sampleTick()
    int cueId() const { return cue_; }
    // P2Z4 -- per-tick recorder.  CALL FROM INSIDE the game's FixedUpdate (same Unity main thread, once per physics tick), NOT from Update/OnGUI.
    // Read-only (getters only).  Fails closed: any getter that returns null => ok=false for that field and `why` names it; nothing is written to the game.
    struct TickBall { live::Vec3 pos, vel, ang; int kin = -1; double damp = -1; int sleeping = -1; bool ok = false; };
    struct TickRec { double fixedTime = -1; int mode = -1; int hit = -1; std::vector<TickBall> b; };
    bool sampleTick(TickRec& out, std::string& why);
    // Log line: `#tick T=<fixedTime %.6f> mode=<simulationMode> hit=<isHit>` then per ball ` i:x,y,z|vx,vy,vz|k|damp|s` (all %.6f; ERR for a failed read).
    static std::string formatTick(const TickRec& r, long tickNo);
    // true when every ball position AND every velocity is exactly 0 (the symptom seen in pp2_shot_2/3: probably a broken Vector3 read, not a real stop).
    static bool looksBroken(const TickRec& r);
    live::ShotIn readShot();                // direction/F from PhysicsBuilder, prediction fields from CueBall + SpinController (fields named in dump.cs)
    double ballRadius = 0.0275, contactOffset = 0.001;   // R: per table (engine kTables); defaultContactOffset: data.unity3d (P0)
    int castMask = 0xC00;                    // SpinController.spinLayer (P2R)
    // live::Host
    int count() override { return (int)bodies_.size(); }
    bool get(int i, live::BallState& s) override; bool set(int i, const live::BallState& s) override;
    bool step(double dt) override; double fixedDelta() override { return 1411199.0 / 141120000.0; }
    bool castCue(int cueIdx, live::Vec3 dir, double dist) override; live::Contact cueContact(int cueIdx) override;
    bool inPocket(int idx) override; void park(int idx) override;
    std::string lastError;
private:
    struct M { const void* m = nullptr; };
    const Api& A; bool ready_ = false; int cue_ = -1; std::vector<void*> bodies_; void* gm_ = nullptr; void* cueObj_ = nullptr; void* cueClass_ = nullptr;
    const void *rbGetPos = nullptr, *rbSetPos = nullptr, *rbGetRot = nullptr, *rbSetRot = nullptr, *rbGetVel = nullptr, *rbSetVel = nullptr, *rbGetAng = nullptr, *rbSetAng = nullptr,
               *rbSleep = nullptr, *rbIsSleeping = nullptr, *rbWake = nullptr, *phSimulate = nullptr, *phOverlap = nullptr, *phSphereCast = nullptr, *phGetMode = nullptr,
               *rbGetKin = nullptr, *rbGetDamp = nullptr, *timeGetFixed = nullptr, *colAttached = nullptr, *colIsTrigger = nullptr, *cmpCompareTag = nullptr, *gmGetInstance = nullptr, *gmGetBalls = nullptr, *ballGetBody = nullptr, *gpGetInstance = nullptr;
    void *kTime = nullptr, *kRigid = nullptr, *kPhysics = nullptr, *kCollider = nullptr, *kGM = nullptr, *kBall = nullptr, *kCue = nullptr, *kGP = nullptr, *kPB = nullptr, *kSpin = nullptr, *kComponent = nullptr;
    void* inv(const void* m, void* obj, void** args) { void* exc = nullptr; void* r = A.runtime_invoke(m, obj, args, &exc); if (exc) { lastError = "il2cpp exception"; return nullptr; } return r; }
    bool vec3(const void* m, void* obj, float* out) { void* r = inv(m, obj, nullptr); if (!r) return false; std::memcpy(out, A.object_unbox(r), 12); return true; }
    bool quat(const void* m, void* obj, float* out) { void* r = inv(m, obj, nullptr); if (!r) return false; std::memcpy(out, A.object_unbox(r), 16); return true; }
    bool tagIs(void* comp, const char* tag);
    int bodyIndex(void* rb) const { for (size_t i = 0; i < bodies_.size(); ++i) if (bodies_[i] == rb) return (int)i; return -1; }
};
}}  // namespace pp2::il2
