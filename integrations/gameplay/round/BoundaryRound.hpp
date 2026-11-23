#pragma once

#include "RoundRadar.hpp"
#include <cmath>

namespace brt { namespace round {

// 归原来的 InGameMenu 按下绑定调用。保持焦点和原分支，仅在自己获准的死亡窗口打开选人。
template<class Controller, class DeathPress, class ClearField>
void DispatchInGameMenu(Controller& pc, bool higherModal, bool ownsOpenField, bool stockOpensExit,
                        DeathPress&& pressDeath, ClearField&& closeField) {
    if (!pc.IsLocalController() || higherModal) return;
    if (ownsOpenField) {
        closeField();
        pc.CloseInGameSelectRoleMenu();
        return;
    }
    if (pressDeath()) return;
    if (stockOpensExit) pc.OpenExitMatchInGameMenu();
    else pc.ToggleInGameSelectRoleMenu();
}

// 所属玩家的最后一次捕获与本次绘制资格取交集。腰射手动标记、探针与共享标记
// 都走游戏已经裁剪过的捕获结果。远距、敌锁定我和只开火/过热不会产生补点。
template<class Camp, class MarkableType, class Info>
bool EligibleEnemy(const Info& info) {
    return info.Camp == Camp::Enemy && info.MarkableType == MarkableType::Character &&
           info.bMarked && !info.bIsLocal && info.Texture &&
           std::isfinite(static_cast<double>(info.position.X)) &&
           std::isfinite(static_cast<double>(info.position.Y));
}

// UE 的 TPair 用字段 Key/Value。宿主从正常绘制循环提供经过全部原有显隐过滤的目标，
// 以及这一帧已经加入原生画布的目标。两个集合都以同一个 Markable 为键。
template<class Camp, class MarkableType, class Hud, class Markable, class Node,
         class Key, class Handle, class Range, class Factory, class FrameSets>
class NativeRadarSurface {
    Hud& hud_;
    Factory& factory_;
    FrameSets& frame_;
public:
    NativeRadarSurface(Hud& hud, Factory& factory, FrameSets& frame)
        : hud_(hud), factory_(factory), frame_(frame) {}
    template<class Info>
    bool IsMarkedEnemy(const Key& key, const Info& info) {
        return factory_.ValidTarget(key) && frame_.Eligible(key) &&
               EligibleEnemy<Camp, MarkableType>(info);
    }
    bool StockDrew(const Key& key) const { return frame_.Drawn(key); }
    template<class Info>
    Handle Add(const Info&) {
        Node* node = factory_.MakeNode();
        if (!node) return {};
        factory_.AddToNativeCanvas(node);
        if (!node->GetParent()) {
            node->RemoveFromParent();
            return {};
        }
        return factory_.WeakNode(node);
    }
    bool Alive(const Handle& key) const {
        Node* node = factory_.Resolve(key);
        return node && node->GetParent();
    }
    template<class Info>
    void Update(const Handle& key, const Info& info) {
        if (Node* node = factory_.Resolve(key)) node->UpdateNode(info);
    }
    void Remove(const Handle& key) {
        if (Node* node = factory_.Resolve(key)) node->RemoveFromParent();
    }
    template<class Visit>
    void VisitCapture(Visit&& visit) {
        // 只在正常小地图刚完成一次捕获时调用；每次仅取一次原生快照。
        const auto snapshot = hud_.GetLastCaptureInfoNodeMap();
        Range::Each(snapshot, [&](Markable* target, const auto& info) {
            if (target) visit(factory_.WeakTarget(target), info);
        });
    }
};

// 构造时绑定真实 owning controller。回包走通常的 owner-only 复制/RPC。
// 客户端的 controller 和服务器的 controller 是不同对象。
template<class Controller, class PawnKey>
class RoundServer {
    Controller& owner_;
    DeathAuthority<Controller, PawnKey> authority_;
public:
    explicit RoundServer(Controller& owner) : owner_(owner), authority_(owner) {}
    const SelectionState& State() const { return authority_.State(); }

    template<class Mode, class Send>
    bool Spawn(Controller& controller, Life life, PawnKey pawn, Mode& mode, Send&& ownerOnly) {
        if (!authority_.Spawn(controller, life, pawn, mode.IsRoundInProgress())) return false;
        ownerOnly(owner_, State());
        return true;
    }
    template<class Mode, class Send>
    bool FinalDeath(Controller* victim, Life life, PawnKey killedPawn, Mode& mode, Send&& ownerOnly) {
        if (!authority_.Killed(victim, life, killedPawn, mode)) return false;
        ownerOnly(owner_, State());
        return true;
    }
    template<class Mode, class Send>
    bool Refresh(Life life, Mode& mode, Send&& ownerOnly) {
        if (!authority_.RefreshRules(life, mode)) return false;
        ownerOnly(owner_, State());
        return true;
    }
    template<class Send>
    bool End(std::uint64_t round, Send&& ownerOnly) {
        if (!authority_.EndRound(round)) return false;
        ownerOnly(owner_, State());
        return true;
    }
};

} } // namespace brt::round
