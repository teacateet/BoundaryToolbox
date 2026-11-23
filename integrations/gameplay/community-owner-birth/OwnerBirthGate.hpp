#pragma once
#include <string>
#include <string_view>

namespace brt::owner_birth {
enum class Action { Ready, Wait, Stop };
struct Verdict { Action action; const char* reason; };
struct Sample {
    bool sameWindow, deadlinePassed, sameStateAndTransport, authority, samePawn;
    bool wrongController, killed, acknowledged, firing, targeting;
    int deathsBefore, deaths, bulletsBefore, bullets, rocketsBefore, rockets;
    bool linkedController, alive, paused, roleSelected;
};
inline Verdict Inspect(const Sample& s) noexcept {
    if (!s.sameWindow || s.deadlinePassed) return {Action::Stop, "birth-window-ended"};
    if (!s.sameStateAndTransport) return {Action::Stop, "owner-session-changed"};
    if (!s.authority) return {Action::Stop, "authority-lost"};
    if (!s.samePawn || s.wrongController) return {Action::Stop, "owner-pawn-replaced"};
    if (s.killed) return {Action::Stop, "pawn-killed"};
    if (s.acknowledged) return {Action::Stop, "possession-acknowledged"};
    if (s.firing || s.bullets != s.bulletsBefore || s.rockets != s.rocketsBefore)
        return {Action::Stop, "shot-or-projectile-recorded"};
    if (s.targeting) return {Action::Stop, "player-aiming"};
    if (s.deaths < s.deathsBefore || (static_cast<long long>(s.deaths) - s.deathsBefore) > 1)
        return {Action::Stop, "death-counter-discontinuous"};
    if (!s.linkedController) return {Action::Wait, "awaiting-owner-controller"};
    if (!s.alive) return {Action::Wait, "awaiting-alive-state"};
    if (s.paused) return {Action::Wait, "world-paused"};
    if (!s.roleSelected) return {Action::Wait, "awaiting-role-selection"};
    return {Action::Ready, "ready"};
}

// 六个常规干员的原作枪 ID 使用大写 ASCII，FName 的显示大小写可能不同。
// 不把别的角色限定 ID 再拼接到当前干员；档案服务还会检查干员的武器目录。
inline std::string QualifiedGun(std::string_view role, std::string_view raw) {
    if (role.empty() || raw.empty() || raw.size() > 128) return {};
    std::string id(raw);
    for (char& c : id) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return {};
    }
    if (id == "NONE") return {};
    const auto split = id.find('_');
    if (split == std::string::npos) return std::string(role) + "_" + id;
    return std::string_view(id).substr(0, split) == role && split + 1 < id.size() ? id : std::string{};
}
}
