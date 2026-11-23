#include <ProjectTypes.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
struct FPodRow { const char* Item; const char* Class; const char* Reticle; bool Deploy; };
#include <RetailPodRows.inl>
static void Require(bool pass, const char* id, const char* message)
{
    if (!pass) { std::fprintf(stderr, "%s: %s\n",id,message); std::exit(2); }
}
static void CheckItem(const FPodRow& row,bool left)
{
    APBPlayerController pc; APBCharacter player; pc.Pawn=&player;player.Controller=&pc;
    auto holder = row.Deploy ? std::unique_ptr<APBLauncher>(new APBLauncher_Deploy) : std::make_unique<APBLauncher>();
    APBLauncher& pod=*holder; pod.Owner=&player;
    UClass type{row.Class, {"/Test/Retail/Types"}};
    pod.Class = &type;
    (left ? player.CurrentLeftLauncher : player.CurrentRightLauncher)=&pod;
    APBLauncher_Deploy opposite;
    opposite.Owner=&player;
    (left ? player.CurrentRightLauncher : player.CurrentLeftLauncher)=&opposite;
    UStaticMeshComponent first,third,other; AActor projectile;
    first.Owner=&pod; first.ComponentId={7}; pod.Mesh1P=&first;
    third.Owner=&pod; third.ComponentId={8}; pod.Mesh3P=&third;
    other.Owner=&projectile; other.ComponentId={9}; first.Attached={&third,&other};
    UPBLauncherWidget own, persistent, replacement;
    // 共用相同 Blueprint 类，但状态栏的实例从未被 CrossHairWidget 持有。
    UClass widgetType{row.Reticle,{"/Test/Retail/Widgets"}};
    own.Class=&widgetType; persistent.Class=&widgetType; replacement.Class=&widgetType;
    own.OwningLauncher=&pod; persistent.OwningLauncher=&pod; replacement.OwningLauncher=&pod;
    own.Player=&pc; persistent.Player=&pc; replacement.Player=&pc;
    auto hud=own.TakeWidget(), sidebar=persistent.TakeWidget(), next=replacement.TakeWidget();
    auto visible=[](TSharedRef<SWidget> w){return w->Visibility()!=EVisibility::Collapsed;};
    Require(visible(sidebar),row.Item,"persistent slot HUD initially available");
    pod.CrossHairWidget=&own; pod.Standby=true;
    auto parked=pc.BuildView();
    Require(!visible(hud) && visible(sidebar) && parked.count({7}) && !parked.count({8}) && !parked.count({9}),
            row.Item,"late HUD assignment, own 1P only");
    player.CurrentHandsState=EPBHandsState::DeployedWeapon;
    player.CurrentLauncher=row.Deploy ? &pod : &opposite;
    pod.Standby=false;
    pod.CurrentState=EPBLauncherState::Ready;
    if (row.Deploy) player.CurrentHandsState=EPBHandsState::DeployedLauncherController;
    if (!row.Deploy) {
        Require(pc.BuildView().count({7}) && !visible(hud),row.Item,"inactive auxiliary in opposite slot cannot steal foreground");
        pod.Active=true;
    }
    Require(!pc.BuildView().count({7}) && visible(hud) && visible(sidebar),row.Item,"ready native display");
    if(!row.Deploy)
    {
        pod.CurrentState=EPBLauncherState::Standby;pod.bIsFiring=true;
        Require(!pc.BuildView().count({7}) && visible(hud),row.Item,"native fire alongside rifle");
        pod.bIsFiring=false;pod.bInProjectileControlMode=true;
        Require(!pc.BuildView().count({7}) && visible(hud),row.Item,"native alternative trigger");
        pod.bInProjectileControlMode=false;pod.CurrentState=EPBLauncherState::Ready;
    }
    else
    {
        player.CurrentLauncher=&opposite;
        Require(!visible(hud)&&pc.BuildView().count({7}),row.Item,"other deployed slot wins during input handover");
        player.CurrentLauncher=&pod;
    }
    pod.CrossHairWidget=&replacement; pc.BuildView();
    Require(!visible(hud) && visible(next) && visible(sidebar),row.Item,"replacement inherits only its own gate");
    pod.CrossHairWidget=nullptr; pc.BuildView();
    Require(!visible(next) && !pc.BuildView().count({7}) && visible(sidebar),row.Item,"active pod with no dedicated reticle");
    pod.CrossHairWidget=&own;
    pod.CurrentState=EPBLauncherState::Undeploying;pod.Undeploy=true;pc.BuildView();
    Require(!visible(hud) && pc.BuildView().count({7}),row.Item,"native stow");
    pod.CurrentState=EPBLauncherState::Ready;pod.Undeploy=false;
    (left ? player.CurrentLeftLauncher : player.CurrentRightLauncher)=nullptr;
    Require(pc.BuildView().count({7}) && !visible(hud),row.Item,"stale selected actor removed from both slots");
    (left ? player.CurrentLeftLauncher : player.CurrentRightLauncher)=&pod;
    player.Living=false;
    Require(pc.BuildView().count({7}) && !visible(hud) && visible(sidebar),row.Item,"death and lifetime HUD separation");
    pc.Pawn=nullptr; auto dead=pc.BuildView();
    Require(dead.count({7}) && !dead.count({8}) && !dead.count({9}) && !visible(hud),row.Item,"death camera excludes 3P and deployed actors");
    std::printf("pod %s %s %s\n",row.Item,left?"L":"R",row.Deploy?"controller":"auxiliary");
}
void CheckAllPods()
{
    for(const auto& row:Rows) for(bool left:{true,false}) CheckItem(row,left);
}
