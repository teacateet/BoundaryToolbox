#pragma once
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace SDK {
inline long long frame;
inline bool inScope = true;
inline std::wstring savedPath;
struct FString { std::wstring value; explicit FString(const wchar_t* name) : value(name) {} std::wstring ToWString() const { return value; } };
using FName = int;
struct FKey { FName KeyName; };
struct UClass {};
struct UObject {
    bool valid = true;
    int generation = 1;
    virtual UClass* Type() const { return nullptr; }
    bool IsA(UClass* expected) const { return expected && Type() == expected; }
};
template<class T> struct Array : std::vector<T> { using std::vector<T>::vector; int Num() const { return int(this->size()); } };
struct APBCharacter;
struct APBPlayerState : UObject {
    int deaths = 0;
    static UClass* StaticClass() { static UClass c; return &c; }
    UClass* Type() const override { return StaticClass(); }
    int GetDeaths() const { return deaths; }
};
struct APlayerController : UObject {
    bool local = true;
    APBCharacter* pawn = nullptr;
    APBPlayerState* PlayerState = nullptr;
    int pressed = 0, released = 0, down = 0, inputReads = 0;
    bool IsLocalPlayerController() const { return local; }
    APBCharacter* K2_GetPawn() const { return pawn; }
    bool WasInputKeyJustPressed(const FKey& k) { ++inputReads; return k.KeyName == pressed; }
    bool WasInputKeyJustReleased(const FKey& k) { return k.KeyName == released; }
    bool IsInputKeyDown(const FKey& k) { return k.KeyName == down; }
};
struct APBMobilityModule : UObject {
    APBCharacter* OwnerCharacter = nullptr;
    bool bIsEnabled = true, active = false;
    bool IsActivated() const { return active; }
};
struct APBCatchingModule : APBMobilityModule {
    int CurrentHookStatus = 0;
    bool bIsInCoolDown = false;
    static UClass* StaticClass() { static UClass c; return &c; }
    UClass* Type() const override { return StaticClass(); }
};
struct APBCharacter : UObject {
    APlayerController* control = nullptr;
    APBMobilityModule* current = nullptr;
    bool alive = true, killed = false, cooled = true, allowed = true, usingHook = false;
    int slotReads = 0;
    auto* GetController() const { return control; }
    auto* GetCurrentMobilityModule() { ++slotReads; return current; }
    bool IsAlive() const { return alive; }
    bool IsInKilledStatus() const { return killed; }
    bool GetHasCatchingCooledDown() const { return cooled; }
    bool GetIsAllowedCatch() const { return allowed; }
    bool IsInHookUsingState() const { return usingHook; }
};
struct UPBOptions : UObject {
    int queries = 0;
    Array<FKey> actual;
    static UClass* StaticClass() { static UClass c; return &c; }
    auto GetKeysFromActionName(FName action) { if (action != 132) throw std::runtime_error("wrong action"); ++queries; return actual; }
};
inline UPBOptions* options = nullptr;
struct USubsystemBlueprintLibrary { static auto* GetEngineSubsystem(UClass* actual) { return actual == UPBOptions::StaticClass() ? options : nullptr; } };
struct UKismetStringLibrary { static FName Conv_StringToName(const FString& s) { return s.value == L"ActivateMobility" ? 132 : 0; } };
struct UKismetInputLibrary { static bool Key_IsValid(const FKey& key) { return key.KeyName > 0; } };
struct UKismetSystemLibrary {
    static bool IsValid(UObject* o) { return o && o->valid; }
    static long long GetFrameCount() { return frame; }
    static FString GetProjectSavedDirectory() { return FString(savedPath.c_str()); }
};
}
namespace BrtSparrowReticle {
struct Instance {
    SDK::UObject* ref;
    int generation;
    explicit Instance(SDK::UObject* obj) : ref(obj), generation(obj->generation) {}
    SDK::UObject* Resolve() const { return ref->valid && ref->generation == generation ? ref : nullptr; }
};
inline bool Event(std::string_view name, std::string_view leaf) {
    return name == leaf || (name.size() > leaf.size() && name[name.size()-leaf.size()-1] == '.' && name.ends_with(leaf));
}
}
namespace BrtHeadRadar { inline bool InScope(SDK::APBCharacter*) { return SDK::inScope; } }
inline std::vector<std::string> records;
inline void ClientLog(const std::string& text) { records.push_back(text); }
