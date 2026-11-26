#pragma once

#include <chrono>
#include <string_view>

namespace BrtDeathMenu
{
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Id = const void*;

// 一个原生按钮回调会嵌套触发选角确认、关闭和 CommonUI 更新。最外层返回前不采样半成品菜单栈。
struct NativeRoleAction
{
    static inline thread_local unsigned depth{};
    bool held{};
    explicit NativeRoleAction(bool enter) : held(enter) { if (held) ++depth; }
    NativeRoleAction(const NativeRoleAction&) = delete;
    void Finish() { if (held) { held = false; --depth; } }
    ~NativeRoleAction() { Finish(); }
};

inline bool IsLocalTarget(std::string_view target)
{
    constexpr std::string_view prefix = "127.0.0.1:";
    if (target.substr(0, prefix.size()) != prefix)
        return false;
    const auto port = target.substr(prefix.size());
    if (port.empty() || port.size() > 5)
        return false;
    unsigned number = 0;
    for (const char digit : port)
    {
        if (digit < '0' || digit > '9')
            return false;
        number = number * 10 + (digit - '0');
    }
    return number > 0 && number <= 65535;
}

inline bool IsPveClass(std::string_view path)
{
    return path == "/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Easy.BP_PBGameMode_Rush_PVE_Easy_C"
        || path == "/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Normal.BP_PBGameMode_Rush_PVE_Normal_C"
        || path == "/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Hard.BP_PBGameMode_Rush_PVE_Hard_C";
}

// 只在本次本机切图后绑定新的玩法世界。旧世界、断线和后续切图均不能继承资格。
struct Room
{
    Id origin{}, gameInstance{}, localPlayer{}, playWorld{}, connection{}, modeClass{};
    Time expires{};

    void Travel(bool selected, std::string_view target, Id source, Id instance, Id local, Time now)
    {
        *this = {};
        if (selected && IsLocalTarget(target) && source && instance && local)
        {
            origin = source;
            gameInstance = instance;
            localPlayer = local;
            expires = now + std::chrono::seconds(120);
        }
    }

    bool Check(Id world, Id instance, Id local, Id server, Time now)
    {
        if (!origin)
            return false;
        if (instance != gameInstance || local != localPlayer ||
            (playWorld && (world != playWorld || server != connection)) ||
            (!playWorld && now > expires))
        {
            *this = {};
            return false;
        }
        return world && world != origin && server;
    }

    void Bind(Id world, Id server, Id mode)
    {
        playWorld = world;
        connection = server;
        modeClass = mode;
    }
};

enum class Prompt { None, Initializing, Leave, Other };
enum class Action { None, Back, OpenRoles };
struct Frame
{
    Id controller{}, state{}, layout{}, prompt{};
    Prompt kind{Prompt::None};
    bool menuInFront{}, regularMenuOpen{}, pressed{}, down{}, canSelect{};
    bool aliveSnapshot{}, cancelledResidue{}, keySampled{};
    int deaths{};
};

struct Gesture
{
    enum class Phase { Idle, WaitingPrompt, WaitingPermission, WaitingOnEmptyStack, WaitingClose };
    Phase phase{Phase::Idle};
    Frame previous{};
    Id closing{}, firstPrompt{};
    Time previousTime{}, until{};
    bool previousClean{}, edgeConsumed{};

    void RememberAlive(const Frame& f, Time now)
    {
        *this = {};
        previous = f;
        previousTime = now;
        previousClean = !f.menuInFront && !f.regularMenuOpen && (!f.prompt || f.cancelledResidue) && !f.down && !f.pressed;
        edgeConsumed = f.down || f.pressed;
    }

    Action Observe(const Frame& f, Time now)
    {
        using namespace std::chrono;
        const bool same = f.controller && f.controller == previous.controller &&
            f.state == previous.state && f.layout == previous.layout;
        const bool clean = !f.menuInFront && !f.regularMenuOpen && !f.prompt;
        // 存活快照只用来证明界面此前干净。必须收到真正的新按下，不能把战斗中一直按着的键带到死亡。
        const bool deathEdge = f.pressed && f.deaths > 0 && f.deaths >= previous.deaths &&
            f.deaths - previous.deaths <= 1 && !edgeConsumed;
        const bool firstCleanEdge = !previous.controller && clean && f.pressed && f.deaths > 0 && !edgeConsumed;
        const bool freshPress = (f.pressed || (!previous.aliveSnapshot && f.down && !previous.down)) && !edgeConsumed;
        // 旧 Cancel 弹窗只在新死亡的真实按下把原生普通菜单从关变开、且文本仍是 Leave 时可相关。
        // 原生可能先消费 JustPressed。只有上一帧已经死于同一条命、确实读过菜单键且是松开的，才认按下边沿。
        const bool sampledDeadRise = !previous.aliveSnapshot && previous.keySampled && f.keySampled &&
            previous.deaths > 0 && f.deaths == previous.deaths && !previous.pressed && !previous.down && f.down;
        const bool reopenedCancelled = previous.cancelledResidue && (f.pressed || sampledDeadRise) && freshPress &&
            !previous.regularMenuOpen && f.regularMenuOpen && previous.prompt && previous.prompt == f.prompt && f.kind == Prompt::Leave;
        const bool recentlyClean = same && previousClean && now - previousTime <= milliseconds(250) &&
            (!previous.aliveSnapshot || deathEdge) && (!previous.cancelledResidue || reopenedCancelled);
        const bool nativeOpen = recentlyClean && !previous.aliveSnapshot && f.regularMenuOpen && !previous.regularMenuOpen;
        if (f.pressed || f.down)
            edgeConsumed = true;
        else
            edgeConsumed = false;
        previous = f;
        previousClean = clean || f.cancelledResidue;
        previousTime = now;
        if ((!same && !firstCleanEdge) || f.menuInFront)
        {
            phase = Phase::Idle;
            firstPrompt = closing = nullptr;
            return Action::None;
        }
        if (phase != Phase::Idle && now > until)
            phase = Phase::Idle;
        if (phase == Phase::WaitingOnEmptyStack)
        {
            if (!clean || freshPress || f.down)
                phase = Phase::Idle;
            else if (f.canSelect)
            {
                phase = Phase::Idle;
                return Action::OpenRoles;
            }
            return Action::None;
        }
        if (phase == Phase::WaitingClose)
        {
            if (!f.canSelect || freshPress || (f.prompt && (f.prompt != closing || f.kind == Prompt::Other)))
                phase = Phase::Idle;
            else if (!f.prompt && !f.regularMenuOpen)
            {
                phase = Phase::Idle;
                return Action::OpenRoles;
            }
            return Action::None;
        }
        if (phase == Phase::WaitingPermission)
        {
            if (freshPress || f.prompt != firstPrompt || f.kind != Prompt::Leave)
                phase = Phase::Idle;
            else if (f.canSelect)
            {
                closing = f.prompt;
                phase = Phase::WaitingClose;
                until = now + milliseconds(900);
                return Action::Back;
            }
            return Action::None;
        }
        if (phase == Phase::Idle && ((freshPress || nativeOpen) && recentlyClean || firstCleanEdge))
        {
            phase = Phase::WaitingPrompt;
            firstPrompt = nullptr;
            until = now + milliseconds(300);
        }
        if (phase != Phase::WaitingPrompt)
            return Action::None;
        if ((firstPrompt && firstPrompt != f.prompt) || f.kind == Prompt::Other)
        {
            phase = Phase::Idle;
            return Action::None;
        }
        if (f.prompt)
            firstPrompt = f.prompt;
        // 死亡窗口可能根本不创建离场弹窗。完整的一次按下和松开都在空栈，
        // 才能直接打开原选角页；有弹窗时仍走下面原生 Back 路径。
        if (clean && !f.pressed && !f.down && !firstPrompt)
        {
            if (f.canSelect)
            {
                phase = Phase::Idle;
                return Action::OpenRoles;
            }
            phase = Phase::WaitingOnEmptyStack;
            until = now + seconds(3);
        }
        if (f.kind == Prompt::Leave)
        {
            if (!f.canSelect)
            {
                // 冷却提示与离场弹窗可先于选角许可；只保留同一弹窗上的这次按键。
                phase = Phase::WaitingPermission;
                until = now + seconds(3);
                return Action::None;
            }
            closing = f.prompt;
            phase = Phase::WaitingClose;
            until = now + milliseconds(900);
            return Action::Back;
        }
        return Action::None;
    }
};
}
