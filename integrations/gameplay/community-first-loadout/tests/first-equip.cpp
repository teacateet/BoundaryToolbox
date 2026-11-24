#include <cassert>
#include <chrono>
#include <iostream>

namespace SDK {
struct Type { static const void* StaticClass(){return nullptr;} bool IsA(const void*){return true;} };
struct APBPlayerController; struct APBWeapon; struct UPBLocalPlayer;
enum class EPBHandsState { EmptyHand, DeployedWeapon, DoingMelee };
struct APBPlayerState : Type { int deaths{}; int GetDeaths(){return deaths;} };
struct APBCharacter {
    APBPlayerController* controller{}; APBWeapon *CurrentWeapon{}, *PendingWeapon{};
    EPBHandsState CurrentHandsState{}, PendingHandsState{};
    struct { APBWeapon* first{}; int Num(){return first ? 1 : 0;} APBWeapon* operator[](int){return first;} } Inventory;
    bool alive{true}; int submissions{};
    auto GetController(){return controller;} bool IsAlive(){return alive;} bool IsInKilledStatus(){return !alive;}
    bool IsFirstPerson(){return true;}
    void EquipPendingWeapon(APBWeapon* weapon){assert(weapon == Inventory.first); ++submissions; PendingWeapon=weapon;}
};
struct APBWeapon { APBPlayerController* TopOwner{}; APBCharacter* pawn{}; bool bIsDropped{};
    auto GetPawnOwner(){return pawn;}
};
struct APBPlayerController : Type {
    UPBLocalPlayer* Player{}; APBPlayerState* PlayerState{}; APBCharacter *pawn{}, *view{}; bool bShowSelectRole{};
    bool IsLocalPlayerController(){return true;} bool IsInGameMenuOpened(){return false;}
    auto GetPBCharacter(){return pawn;} auto K2_GetPawn(){return pawn;} auto GetViewTarget(){return view;}
};
struct UPBLocalPlayer { APBPlayerController* PlayerController{}; };
struct APBGameState : Type {
    bool active{}; int mode{};
    struct { const void* ptr{}; const void* Get(){return ptr;} } GameModeClass;
    bool IsRoundInProgress(){return active;}
};
struct Driver { const void* ServerConnection{}; };
struct UWorld { Driver* NetDriver{}; APBGameState* GameState{}; };
struct UKismetSystemLibrary { static bool IsValid(void* p){return p != nullptr;} };
}
namespace BrtDeathMenu {
using Time=std::chrono::steady_clock::time_point;
struct Clock { static inline Time current{}; static Time now(){return current;} };
inline bool reading{};
inline struct {const void *playWorld{}, *localPlayer{}, *connection{}, *modeClass{};} room;
}
inline void ClientDebugLog(const char*) {}
#include "../FirstEquip.hpp"

struct Session {
    SDK::APBCharacter pawn, other;
    SDK::APBWeapon gun;
    SDK::APBPlayerController pc;
    SDK::UPBLocalPlayer local{&pc};
    SDK::APBPlayerState ps;
    SDK::Driver driver{this};
    SDK::APBGameState gs;
    SDK::UWorld world{&driver,&gs};
    Session() {
        BrtFirstEquip::opening={};
        BrtDeathMenu::Clock::current={};
        BrtDeathMenu::room={&world,&local,this,&gs};
        gs.GameModeClass.ptr=&gs;
        pawn.controller=&pc; pc.Player=&local; pc.PlayerState=&ps; pc.pawn=pc.view=&pawn;
        gun.TopOwner=&pc; gun.pawn=&pawn; pawn.Inventory.first=&gun;
    }
    void at(int ms) { BrtDeathMenu::Clock::current=BrtDeathMenu::Time{}+std::chrono::milliseconds(ms);
        BrtFirstEquip::Tick(&world,&local); }
};

int main() {
    { Session s; s.pawn.CurrentWeapon=&s.gun; s.at(0); s.pawn.CurrentWeapon=nullptr; s.pc.view=nullptr;
      s.gs.active=true; s.at(5000); s.pc.view=&s.pawn; s.at(6000); s.at(6700); assert(s.pawn.submissions==0);
      s.at(6800); s.at(12000); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.at(0); s.pawn.PendingWeapon=&s.gun; s.at(700);
      s.pawn.PendingWeapon=nullptr; s.at(5000); assert(s.pawn.submissions==0);
      s.at(5800); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.at(0); s.pc.bShowSelectRole=true; s.at(1000); s.pc.bShowSelectRole=false;
      s.at(2000); assert(s.pawn.submissions==0); s.at(2800); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.gun.TopOwner=nullptr; s.at(0); s.at(1500); assert(s.pawn.submissions==0);
      s.gun.TopOwner=&s.pc; s.at(2500); assert(s.pawn.submissions==0); s.at(3300); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.at(0); s.pawn.PendingHandsState=SDK::EPBHandsState::DoingMelee;
      s.at(700); s.pawn.PendingHandsState={}; s.at(5000); assert(s.pawn.submissions==0); }
    { Session s; s.gs.active=true; s.at(0); s.ps.deaths=1; s.at(900); assert(s.pawn.submissions==0); }
    { Session s; s.gs.active=true; BrtDeathMenu::room.connection=nullptr; s.at(0); s.at(900); assert(s.pawn.submissions==0); }
    { Session s; s.gs.active=true; s.at(0); s.other.controller=&s.pc; s.pc.view=s.pc.pawn=&s.other;
      s.at(200); s.pc.view=s.pc.pawn=&s.pawn;
      s.at(1400); assert(s.pawn.submissions==0); }
    { Session s; s.gs.active=true; s.at(0); s.pc.pawn=nullptr; s.at(300); s.pc.pawn=&s.pawn;
      s.at(700); s.at(1500); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.pawn.CurrentHandsState=s.pawn.PendingHandsState=SDK::EPBHandsState::DeployedWeapon;
      s.pawn.CurrentWeapon=&s.gun; s.at(0); s.at(600); s.pawn.CurrentWeapon=nullptr;
      s.pawn.CurrentHandsState=s.pawn.PendingHandsState={}; s.at(900); s.at(1700); assert(s.pawn.submissions==1); }
    { Session s; s.gs.active=true; s.pawn.CurrentHandsState=s.pawn.PendingHandsState=SDK::EPBHandsState::DeployedWeapon;
      s.pawn.CurrentWeapon=&s.gun; s.at(0); s.at(2000); s.pawn.CurrentWeapon=nullptr;
      s.pawn.CurrentHandsState=s.pawn.PendingHandsState={}; s.at(2100); s.at(4000); assert(s.pawn.submissions==0); }
    std::cout << "11 first-life scenarios passed\n";
}
