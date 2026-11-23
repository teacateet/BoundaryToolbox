#pragma once
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

// 只模拟状态机调用的公开游戏接口，每次成功生成都产生独立的角色和挂载。
namespace SDK {
enum class Kind { Other, PlayerState, Controller, Spectator };
struct UObject {
    virtual ~UObject() = default;
    virtual bool IsA(Kind) const { return false; }
    std::string GetFullName() const { return "fixture"; }
};
struct AController : UObject {};
struct APawn : UObject {
    AController* Controller{};
    AController* GetController() const { return Controller; }
    void ForceNetUpdate() {}
};
struct ASpectatorPawn : APawn {
    static Kind StaticClass() { return Kind::Spectator; }
    bool IsA(Kind kind) const override { return kind == StaticClass(); }
};
struct Attachment {};
struct APBCharacter : APawn {
    Attachment left, right;
    bool alive = true, killed = false;
    bool IsAlive() const { return alive; }
    bool IsInKilledStatus() const { return killed; }
};
struct APBPlayerState : UObject {
    bool selected = false;
    static Kind StaticClass() { return Kind::PlayerState; }
    bool IsA(Kind kind) const override { return kind == StaticClass(); }
    bool HasSelectedRole() const { return selected; }
};
struct ViewTransition {};
struct APBPlayerController : AController {
    APawn* Pawn{};
    APawn* AcknowledgedPawn{};
    APawn* view{};
    APawn* lastRetry{};
    UObject* PlayerState{};
    int cameraCalls{}, clientRestarts{}, confirmations{}, quickRespawns{}, suicides{}, rolePrompts{};
    static Kind StaticClass() { return Kind::Controller; }
    bool IsA(Kind kind) const override { return kind == StaticClass(); }
    APawn* K2_GetPawn() const { return Pawn; }
    APBCharacter* GetPBCharacter() const { return dynamic_cast<APBCharacter*>(Pawn); }
    void Possess(APawn* pawn) { Pawn = pawn; pawn->Controller = this; }
    void UnPossess() { if (Pawn) Pawn->Controller = nullptr; Pawn = nullptr; }
    void ClientSetViewTarget(APawn* pawn, ViewTransition) { view = pawn; ++cameraCalls; }
    void ClientRestart(APawn* pawn) { view = pawn; ++clientRestarts; }
    void ClientRetryClientRestart(APawn* pawn) { lastRetry = pawn; }
    void ServerAcknowledgePossession(APawn* pawn) { AcknowledgedPawn = pawn; ++confirmations; }
    void ServerQuickRespawn() { ++quickRespawns; }
    void ServerSuicide(int) { ++suicides; }
    void ClientSelectRole() { ++rolePrompts; }
    void ServerSetSpectatorWaiting(bool) {}
    void ClientSetSpectatorWaiting(bool) {}
    void SetIgnoreMoveInput(bool) {}
    void SetIgnoreLookInput(bool) {}
    void ClientIgnoreMoveInput(bool) {}
    void ClientIgnoreLookInput(bool) {}
    void ClientStartOnlineGame() {}
    void ClientMatchHasStarted() {}
    void ClientRoundHasStarted() {}
    void NotifyGameStarted() {}
    void ExitObserverState() {}
    void ForceNetUpdate() {}
    void ClientReadyAtStartSpot() {}
    void ClientGotoState(const wchar_t*) {}
};
template<class T> struct TArray : std::vector<T> {
    void Add(T value) { this->push_back(value); }
};
struct APBGameState { bool IsRoundInProgress() const { return false; } };
struct AGameMode { APBGameState* GameState{}; };
struct APBGameMode : AGameMode {
    int requests{};
    bool spawnSucceeds = true;
    std::deque<APBCharacter> generated;
    void RestartPlayers(const TArray<AController*>& controllers) {
        if (controllers.size() != 1) throw std::runtime_error("expected single-player restart");
        auto* pc = static_cast<APBPlayerController*>(controllers.front());
        ++requests;
        if (spawnSucceeds) pc->Possess(&generated.emplace_back());
    }
};
struct UWorld {
    AGameMode* AuthorityGameMode{};
    static UWorld* GetWorld() { static UWorld world; return &world; }
};
struct UKismetStringLibrary {
    static const wchar_t* Conv_StringToName(const wchar_t* value) { return value; }
};
namespace Params {
struct PBGameMode_CanPlayerSelectRole { APBPlayerController* Player{}; bool ReturnValue{}; };
struct PBPlayerController_CanSelectRole { bool ReturnValue{}; };
}
}
inline void ServerDebugLog(const std::string&) {}
inline void BrtPreorderOwnerInventory(SDK::AGameMode*, SDK::APBPlayerController*) {}
inline void BrtTickPendingOwnerReconnect() {}
inline void BrtTickOwnerBirth() {}
