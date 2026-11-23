#include "Retry.hpp"
#include <cassert>
#include <iostream>
using namespace brt;
std::optional<spawn::RoomHttp> service;
std::optional<reconnect::Menu> delayed;
std::string account;
int callbacks=0;
void BrtPreorderOwnerInventory(SDK::AGameMode*,SDK::APBPlayerController* player) noexcept {
    try {
        ++callbacks;
        delayed=reconnect::ReadMenu(*service,account,*player);
    }catch(const std::exception&) {}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    try {
        auto room=spawn::RoomHttp::FromLocalServer(static_cast<unsigned short>(std::stoi(argv[1])));
        if(!room)return 3;
        const std::string owner=argv[2];
        service=room;account=owner;
        SDK::APBPlayerController old{{1,1}},replacement{{2,1}},visitor{{3,1}};
        SDK::UNetConnection wire{{4,1},&old},second{{5,1},&replacement},outside{{6,1},&visitor};
        SDK::AGameMode mode{{7,1}};SDK::APBPlayerState playerState{{8,1}};
        replacement.PlayerState=&playerState;
        old.NetConnection=&wire;replacement.NetConnection=&second;visitor.NetConnection=&outside;
        bool forbidden=false;
        try {reconnect::ReadMenu(*room,"76561198059932077",visitor);} catch(const spawn::RoomHttpStatus& e) {forbidden=e.status==403;}
        assert(forbidden);
        auto first=reconnect::ReadMenu(*room,owner,old);
        assert(first && first->rows.size()==6);
        auto retry=reconnect::ReadMenu(*room,owner,old);
        assert(retry && retry->member.connection==first->member.connection);
        assert(!reconnect::ReadMenu(*room,owner,replacement));
        auto* firstLogin = reconnect::connections.VerifiedEntry(first->member.connection);
        firstLogin->menuHandled = firstLogin->birthReady = true;
        room->MenuSubmitted(first->member);
        assert(!reconnect::ReadMenu(*room,owner,old));
        assert(!reconnect::ReadMenu(*room,owner,replacement));
        reconnect::WaitForDisconnect(mode,replacement,room->RoomId(),owner);
        reconnect::Tick();assert(callbacks==0);
        playerState.selected=true;
        reconnect::Tick();assert(callbacks==0 && reconnect::pending);
        wire.PlayerController=nullptr;
        reconnect::Tick();reconnect::Tick();
        assert(callbacks==1);
        const auto& newMenu=delayed;
        assert(newMenu && newMenu->rows.size()==6 && newMenu->member.connection!=first->member.connection);
        assert(playerState.selected && !reconnect::pending);
        auto* newLogin = reconnect::connections.VerifiedEntry(newMenu->member.connection);
        newLogin->menuHandled = newLogin->birthReady = true;
        assert(!reconnect::ReadMenu(*room,owner,replacement));
        // 同一个存储地址被新一代控制器和传输复用时，原来的弱标识必须失效。
        ++replacement.serial;++second.serial;
        const auto next=reconnect::ReadMenu(*room,owner,replacement);
        assert(next && next->member.connection!=newMenu->member.connection);
        std::cout<<"{\"sameController\":true,\"oldControllerBlocksReplacement\":true,\"detachedTransportAllowsReconnect\":true,\"overlappingLoginRecovered\":true,\"selectedPendingPlayerPreserved\":true,\"recycledObjectGeneration\":true,\"rows\":6}";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what();return 5;}
}
