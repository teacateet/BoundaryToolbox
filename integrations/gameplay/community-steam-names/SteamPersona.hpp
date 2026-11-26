#pragma once
#include "Persona.hpp"
#include <type_traits>
#include "SDK/UMG_PlayerInfo_InGame_classes.hpp"
#include "SDK/UMG_PlayerState_Tip_classes.hpp"

namespace brt::steam_names {
using namespace SDK;
using namespace brt::persona;

// 只复用游戏已初始化的 Steamworks 1.57，不加载 DLL、启动 Steam 或创建认证会话。
struct Steam {
    struct Functions {
        using Handle = int(__cdecl*)();
        using Interface = void*(__cdecl*)();
        Handle pipe{}, userHandle{};
        Interface userInterface{}, friendsInterface{};
        bool (__cdecl* loggedOn)(void*){};
        Id (__cdecl* steamId)(void*){};
        const char* (__cdecl* selfName)(void*){};
        const char* (__cdecl* peerName)(void*,Id){};
        bool (__cdecl* request)(void*,Id,bool){};
    };
    void* friends{};
    const Functions* fn{};
    Id id{};
    const char* Self() const {return fn->selfName(friends);}
    bool Pending(Id peer) const {return fn->request(friends,peer,true);}
    const char* Peer(Id peer) const {return fn->peerName(friends,peer);}
    static std::optional<Steam> Open() {
        auto mod=GetModuleHandleW(L"steam_api64.dll");
        if (!mod) return {};
        static HMODULE last{};
        static Functions f;
        if (last!=mod) {
            last=mod;
            const auto bind=[&](auto& slot,const char* sym) {
                slot=reinterpret_cast<std::remove_reference_t<decltype(slot)>>(GetProcAddress(mod,sym));
            };
            bind(f.pipe,"SteamAPI_GetHSteamPipe"); bind(f.userHandle,"SteamAPI_GetHSteamUser");
            bind(f.userInterface,"SteamAPI_SteamUser_v023"); bind(f.friendsInterface,"SteamAPI_SteamFriends_v017");
            bind(f.loggedOn,"SteamAPI_ISteamUser_BLoggedOn"); bind(f.steamId,"SteamAPI_ISteamUser_GetSteamID");
            bind(f.selfName,"SteamAPI_ISteamFriends_GetPersonaName");
            bind(f.peerName,"SteamAPI_ISteamFriends_GetFriendPersonaName"); bind(f.request,"SteamAPI_ISteamFriends_RequestUserInformation");
        }
        if (!f.pipe || !f.userHandle || !f.userInterface || !f.friendsInterface || !f.loggedOn || !f.steamId ||
            !f.selfName || !f.peerName || !f.request || !f.pipe() || !f.userHandle()) return {};
        auto* user=f.userInterface(); auto* friends=f.friendsInterface();
        if (!user || !friends || !f.loggedOn(user)) return {};
        const Id id=f.steamId(user);
        return Individual(id) ? std::optional<Steam>(Steam{friends,&f,id}) : std::nullopt;
    }
};

inline bool Member(APBGameState* gs, APBPlayerState* state) {
    if (!UKismetSystemLibrary::IsValid(state) || state->bIsABot || state->bIsInactive) return false;
    for (auto* listed : gs->PlayerArray) if (listed==state) return true;
    return false;
}
inline bool Visible(UWidget* widget) {
    if (!UKismetSystemLibrary::IsValid(widget)) return false;
    for (auto* item=widget; item; item=item->GetParent())
        if (!item->IsVisible() || !(item->GetRenderOpacity()>0)) return false;
    const auto size=USlateBlueprintLibrary::GetLocalSize(widget->GetCachedGeometry());
    return size.X>0 && size.Y>0;
}
inline void ShowRows(APBPlayerController* pc, APBPlayerState* me, APBGameState* gs, Steam& steam, Id own) {
    auto* type=UUMG_PlayerInfo_InGame_C::StaticClass();
    if (!type) return;
    TArray<UUserWidget*> list;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(pc,&list,type,false);
    for (auto* entry : list) {
        if (!UKismetSystemLibrary::IsValid(entry) || entry->GetOwningPlayer()!=pc || !Visible(entry)) continue;
        auto* row=static_cast<UUMG_PlayerInfo_InGame_C*>(entry);
        auto* member=row->CurrentPlayerState;
        if (member!=row->PlayerStateUI.PlayerState || !Member(gs,member)) continue;
        auto* tip=row->UMG_PlayerState_Tip_Player;
        auto* text=UKismetSystemLibrary::IsValid(tip) ? tip->Text_Name : nullptr;
        if (!Visible(text) || tip->GetOwningPlayer()!=pc) continue;
        const Id id=Account(member->GetDefaultIDStr().ToWString());
        auto name=Read(steam,steam.id,own,id,member==me);
        if (name && UKismetTextLibrary::Conv_TextToString(text->GetText()).ToWString()!=*name)
            text->SetText(UKismetTextLibrary::Conv_StringToText(FString(name->c_str())));
    }
}

inline void Tick(UWorld* world, UPBLocalPlayer* local) {
    static Clock::time_point next{};
    static Publication publication;
    const auto now=Clock::now();
    if (now<next) return;
    // 在调用任何原生控件前限流：UE 回调也会经过工具箱的客户端泵。
    next=now+std::chrono::milliseconds(650);
    auto* conn=world && world->NetDriver ? world->NetDriver->ServerConnection : nullptr;
    auto* controller=local ? local->PlayerController : nullptr;
    if (!conn || !controller || controller->Player!=local || !controller->IsLocalPlayerController() ||
        (conn->OwningActor!=controller && conn->PlayerController!=controller) ||
        !controller->IsA(APBPlayerController::StaticClass()) || !world->GameState ||
        !world->GameState->IsA(APBGameState::StaticClass())) {publication={}; return;}
    auto* pc=static_cast<APBPlayerController*>(controller);
    if (!UKismetSystemLibrary::IsValid(pc->PlayerState) || !pc->PlayerState->IsA(APBPlayerState::StaticClass())) return;
    auto* me=static_cast<APBPlayerState*>(pc->PlayerState);
    auto* gs=static_cast<APBGameState*>(world->GameState);
    if (!Member(gs,me)) return;
    auto* settings=UGameUserSettings::GetGameUserSettings();
    auto* gameSettings=settings && settings->IsA(UProjectBoundaryUserSettings::StaticClass())
        ? static_cast<UProjectBoundaryUserSettings*>(settings) : nullptr;
    // 原版主播模式用匿名表替换姓名；不得把玩家主动隐藏的真实名字写回比分板。
    if (!DisplayEnabled(gameSettings)) {publication={};return;}
    auto steam=Steam::Open();
    if (!steam) return;
    const Id own=Account(me->GetDefaultIDStr().ToWString());
    if (own!=steam->id) return;
    if (auto name=Read(*steam,steam->id,own,own,true)) {
        if (publication.ShouldSubmit(me,own,*name,me->GetPlayerName().ToWString(),now))
            pc->SetName(FString(name->c_str()));
    }
    if (pc->bShowScoreboard) ShowRows(pc,me,gs,*steam,own);
}
}
