#include <Windows.h>
#include <cassert>
#include <string>
#include <vector>

namespace SDK {
struct State { bool bIsInactive = false, bIsABot = false; };
struct PC { State* PlayerState; };
struct Connection { PC* PlayerController; };
struct UWorld;
struct UNetDriver { Connection* ServerConnection = nullptr; std::vector<Connection*> ClientConnections; UWorld* World=nullptr; };
struct GS { std::vector<State*> PlayerArray; };
struct UWorld {
    UNetDriver* NetDriver; bool AuthorityGameMode; GS* GameState;
    inline static UWorld* current = nullptr;
    static UWorld* GetWorld() { return current; }
};
struct UPBLocalPlayer { PC* PlayerController; };
}
#include "../RoomCount.hpp"

int wmain(int argc, wchar_t** argv) {
    using namespace SDK;
    if (argc == 2) {
        State alice, bob, bot{false,true}, stale{true,false};
        PC a{&alice}, b{&bob}, c{&bot}, d{&stale}, pending{nullptr};
        Connection ca{&a}, cb{&b}, cc{&c}, cd{&d}, cp{&pending};
        UNetDriver net{nullptr,{&ca,&cb,&cc,&cd,&cp}};
        GS gs{{&alice,&bob,&bot,&stale}};
        const bool server = std::wstring(argv[1]) == L"host";
        UWorld world{&net,server,&gs}; UWorld::current = &world;
        net.World=&world;
        if (server) { UWorld::current=nullptr; brt::room_count::Server(&net); }
        else {
            net.ServerConnection = &ca;
            UPBLocalPlayer self{&a};
            brt::room_count::Client(&world,&self);
        }
        Sleep(1200);
        return 0;
    }
    for (const auto role : {1,2}) {
        std::wstring cmd = L"\"" + std::wstring(argv[0]) + (role == 1 ? L"\" host" : L"\" guest");
        STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        assert(CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi));
        const auto name = L"Local\\BRT.RoomCount.v1." + std::to_wstring(pi.dwProcessId);
        HANDLE section = nullptr;
        for(int tries=0; !section && tries<100; ++tries) {
            section = OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str()); if (!section) Sleep(10);
        }
        assert(section);
        auto* view = static_cast<const volatile ULONG64*>(MapViewOfFile(section,FILE_MAP_READ,0,0,8));
        assert(view);
        ULONG64 word = 0;
        for (int tries=0; !word && tries<100; ++tries) { word=*view; if (!word) Sleep(10); }
        assert((word & 0xffff) == static_cast<unsigned>((role<<8) + 2));
        assert((word >> 16) <= GetTickCount64() && GetTickCount64() - (word >> 16) < 1500);
        UnmapViewOfFile(const_cast<ULONG64*>(view)); CloseHandle(section);
        assert(WaitForSingleObject(pi.hProcess,3000)==WAIT_OBJECT_0);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        assert(!OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str()));
    }
}
