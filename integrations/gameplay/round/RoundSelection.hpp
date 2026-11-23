#pragma once

#include <chrono>
#include <cstdint>

namespace brt { namespace round {

// 一个实例属于一个已认证控制器。回合序号由玩法分配，生命序号沿用出生流程。
struct Life {
    std::uint64_t round = 0;
    std::uint64_t number = 0;

    bool Valid() const { return round != 0 && number != 0; }
    friend bool operator==(Life a, Life b) { return a.round == b.round && a.number == b.number; }
    friend bool operator!=(Life a, Life b) { return !(a == b); }
    friend bool operator<(Life a, Life b) {
        return a.round < b.round || (a.round == b.round && a.number < b.number);
    }
};

struct SelectionState {
    Life life;
    std::uint64_t revision = 0;
    bool roundPlaying = false;
    bool finalDeath = false;
    bool allowed = false;
};

// 服务器所有者直接调用。权限取自当前 GameMode，真正提交仍走原生的角色确认。
template<class Controller, class PawnIdentity>
class DeathAuthority {
    Controller* owner_;
    PawnIdentity pawn_{};
    SelectionState state_{};
    bool ended_ = false;

    bool Update(bool playing, bool dead, bool allowed) {
        allowed = playing && dead && allowed;
        if (playing == state_.roundPlaying && dead == state_.finalDeath && allowed == state_.allowed)
            return false;
        state_.roundPlaying = playing;
        state_.finalDeath = dead;
        state_.allowed = allowed;
        ++state_.revision;
        return true;
    }
public:
    explicit DeathAuthority(Controller& owner) : owner_(&owner) {}
    DeathAuthority(const DeathAuthority&) = delete;
    DeathAuthority& operator=(const DeathAuthority&) = delete;
    const SelectionState& State() const { return state_; }

    bool Spawn(Controller& forPlayer, Life life, PawnIdentity pawn, bool playing) {
        if (&forPlayer != owner_ || !life.Valid() || pawn == PawnIdentity{} ||
            !(state_.life < life) || (ended_ && state_.life.round == life.round)) return false;
        pawn_ = pawn;
        ended_ = false;
        state_ = {life, 1, playing, false, false};
        return true;
    }
    template<class Mode>
    bool Killed(Controller* killedController, Life life, PawnIdentity killedPawn, Mode& mode) {
        if (killedController != owner_ || !state_.life.Valid() || state_.life != life ||
            killedPawn != pawn_ || ended_) return false;
        const bool playing = mode.IsRoundInProgress();
        return Update(playing, true, playing && mode.CanPlayerSelectRole(owner_));
    }
    template<class Mode>
    bool RefreshRules(Life life, Mode& mode) {
        if (!state_.life.Valid() || state_.life != life || ended_) return false;
        const bool playing = mode.IsRoundInProgress();
        return Update(playing, state_.finalDeath,
                      playing && state_.finalDeath && mode.CanPlayerSelectRole(owner_));
    }
    bool EndRound(std::uint64_t round) {
        if (state_.life.round != round || !state_.life.Valid() || ended_) return false;
        ended_ = true;
        // 回合结束也发布一个版本，以终止已打开菜单和延迟确认。
        if (!Update(false, state_.finalDeath, false)) ++state_.revision;
        return true;
    }
};

enum class KeyResult { PassToExistingMenu, Consumed };

// 对应拥有者的本地控制器。只建立一条 InGameMenu 输入路线；UI 模态优先处理。
template<class Controller, class Role>
class DeathMenu {
public:
    using Clock = std::chrono::steady_clock;
private:
    Controller* owner_;
    SelectionState state_{};
    bool openRequested_ = false;
    bool confirming_ = false;
    bool pending_ = false;
    Clock::time_point until_{};

    bool IsOwnDeath(const Controller& receiver, bool supportedMode) const {
        return &receiver == owner_ && owner_->IsLocalController() && supportedMode &&
               state_.life.Valid() && state_.roundPlaying && state_.finalDeath;
    }
    template<class BeforeOpen>
    void Open(Controller& receiver, BeforeOpen& beforeOpen) {
        pending_ = false;
        openRequested_ = true;
        beforeOpen();
        receiver.OpenInGameSelectRoleMenu();
    }
public:
    explicit DeathMenu(Controller& owner) : owner_(&owner) {}
    DeathMenu(const DeathMenu&) = delete;
    DeathMenu& operator=(const DeathMenu&) = delete;
    const SelectionState& State() const { return state_; }

    bool Receive(Controller& receiver, const SelectionState& next) {
        if (&receiver != owner_ || !receiver.IsLocalController() || !next.life.Valid() ||
            next.revision == 0 || next.life < state_.life ||
            (next.life == state_.life && next.revision <= state_.revision)) return false;
        if (next.life != state_.life || !next.roundPlaying || !next.finalDeath || (state_.allowed && !next.allowed))
            pending_ = false;
        if (next.life != state_.life || !next.roundPlaying || !next.finalDeath || !next.allowed) {
            openRequested_ = false;
            confirming_ = false;
        }
        state_ = next;
        return true;
    }
    bool OffersRole(const Controller& receiver, bool supportedMode) const {
        return IsOwnDeath(receiver, supportedMode) && state_.allowed && owner_->CanSelectRole();
    }
    bool HasPendingPress() const { return pending_; }
    void CancelPendingPress() { pending_ = false; }
    template<class BeforeOpen>
    bool ResumePress(Controller& receiver, bool supportedMode, bool higherModal, bool fieldModOpen,
                     BeforeOpen&& beforeOpen, Clock::time_point now = Clock::now()) {
        if (!pending_) return false;
        if (now >= until_ || !IsOwnDeath(receiver,supportedMode) || higherModal || fieldModOpen || openRequested_) {
            pending_ = false;
            return false;
        }
        if (!OffersRole(receiver,supportedMode)) return false;
        Open(receiver, beforeOpen);
        return true;
    }
    template<class BeforeOpen>
    KeyResult Press(Controller& receiver, bool supportedMode, bool higherModal,
                    bool fieldModOpen, bool isKeyRepeat, BeforeOpen&& beforeOpen,
                    Clock::time_point now = Clock::now()) {
        if (!IsOwnDeath(receiver, supportedMode) || higherModal) {
            pending_ = false;
            return KeyResult::PassToExistingMenu;
        }
        if (pending_ && now >= until_) pending_ = false;
        if (fieldModOpen) pending_ = false;
        if (fieldModOpen || isKeyRepeat || openRequested_) return KeyResult::Consumed;
        if (pending_) {
            pending_ = false;
        } else if (OffersRole(receiver, supportedMode)) {
            Open(receiver, beforeOpen);
        } else {
            // 属主已死亡，本键不能转到离场；只有同一条命稍后拿到双方选角许可才执行。
            pending_ = true;
            until_ = now + std::chrono::seconds(3);
        }
        return KeyResult::Consumed;
    }
    KeyResult Press(Controller& receiver, bool supportedMode, bool higherModal, bool fieldModOpen, bool isKeyRepeat) {
        return Press(receiver,supportedMode,higherModal,fieldModOpen,isKeyRepeat,[] {});
    }
    void OnMenuDismissedOrOpenFailed() { openRequested_ = false; pending_ = false; }
    void OnSelectionResolved(Life displayedLife) {
        if (displayedLife == state_.life) confirming_ = false;
    }
    bool Confirm(Controller& receiver, Life displayedLife, const Role& role, bool supportedMode) {
        if (displayedLife != state_.life || !OffersRole(receiver, supportedMode) || confirming_) return false;
        confirming_ = true;
        receiver.ConfirmRoleSelection(role);
        return true;
    }
};

} } // namespace brt::round
