// 在测试进程中执行完整登录入口，用计数器观察选角、出生登记和房间回执。
#include "OwnerLogin.hpp"
#include <cassert>

int main() {
    SDK::APBPlayerState state;
    SDK::APBPlayerController player;
    SDK::UNetConnection wire{&player};
    SDK::AGameMode mode;
    player.PlayerState = &state;
    player.NetConnection = &wire;
    auto login = [&] { BrtPreorderOwnerInventory(&mode, &player); };

    state.selected = true;
    login(); login();
    assert(calls.birth == 1 && calls.orders == 0 && calls.acks == 0 && calls.Finished());

    calls = {};
    state.selected = false;
    calls.selectOnRead = true;
    login();
    assert(calls.birth == 1 && calls.orders == 0 && calls.acks == 0 && calls.Finished());

    calls = {};
    state.selected = false;
    login(); login();
    assert(calls.birth == 1 && calls.orders == 6 && calls.acks == 1 && calls.Finished());

    calls = {};
    state.selected = true;
    calls.failWatch = true;
    login();
    assert(!calls.Finished() && calls.orders == 0 && calls.acks == 0);
    calls.failWatch = false;
    login();
    assert(calls.birth == 2 && calls.Finished() && calls.orders == 0 && calls.acks == 0);

    calls = {};
    state.selected = false;
    calls.failWatch = true;
    login();
    assert(calls.birth == 1 && calls.menuHandled && !calls.birthReady && calls.orders == 6 && calls.acks == 1);
    state.selected = true;
    calls.failWatch = false;
    login(); login();
    assert(calls.birth == 2 && calls.Finished() && calls.orders == 6 && calls.acks == 1);

    calls = {};
    wire.PlayerController = nullptr;
    login();
    assert(calls.birth == 0 && calls.orders == 0 && !calls.Finished());
    std::puts("PASS selected at login, selected during read, ordinary preorder, retry, detached owner");
}
