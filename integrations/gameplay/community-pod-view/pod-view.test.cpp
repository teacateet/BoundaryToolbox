#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
namespace SDK
{
struct UObject
{
    static inline int last{};
    static inline std::unordered_map<int,UObject*> slots;
    int index=++last, serial=1;
    bool valid=true;
    UObject(){slots[index]=this;}
    virtual ~UObject(){slots.erase(index);}
    virtual bool IsA(int) const{return false;}
};
struct FWeakObjectPtr{int ObjectIndex{},ObjectSerialNumber{};};
template<class T>struct TWeakObjectPtr:FWeakObjectPtr{};
template<class T>struct TSoftObjectPtr{FWeakObjectPtr WeakPtr;};
struct UKismetSystemLibrary
{
    static bool IsValid(const UObject* p){return p && p->valid;}
    static TSoftObjectPtr<UObject> Conv_ObjectToSoftObjectReference(UObject* p){return {{p->index,p->serial}};}
    static UObject* Conv_SoftObjectReferenceToObject(const TSoftObjectPtr<UObject>& p){
        auto it=UObject::slots.find(p.WeakPtr.ObjectIndex);return it==UObject::slots.end()?nullptr:it->second;
    }
};
template<class T>struct Array:std::vector<T>
{
    using std::vector<T>::operator=;
    int Num()const{return static_cast<int>(this->size());}
    void Add(const T& v){this->push_back(v);}
    void Remove(int i){this->erase(this->begin()+i);}
};
struct AActor:UObject{};
struct USceneComponent:UObject
{
    AActor* owner{};
    Array<USceneComponent*> children;
    AActor* GetOwner()const{return owner;}
    int GetNumChildrenComponents()const{return children.Num();}
    USceneComponent* GetChildComponent(int i)const{return children[i];}
};
struct UPrimitiveComponent:USceneComponent
{
    bool authoredVisible=true;
    static int StaticClass(){return 1;}
    bool IsA(int id)const override{return id==StaticClass();}
};
struct APBCharacter;
struct APlayerController:AActor
{
    Array<TWeakObjectPtr<UPrimitiveComponent>> HiddenPrimitiveComponents;
    APBCharacter* pawn{};
    bool local=true;
    bool IsLocalPlayerController()const{return local;}
    APBCharacter* K2_GetPawn()const{return pawn;}
};
enum class EPBLauncherState{Standby,Deploying,Handup,Ready,Reloading,Undeploying};
enum class EPBHandsState{DeployingLauncherController,DeployedLauncherController,DeployedWeapon};
struct APBLauncher:AActor
{
    UPrimitiveComponent *ArmMesh1P{},*ArmMesh3P{},*Mesh1P{},*Mesh3P{};
    APBCharacter* pawn{};
    EPBLauncherState CurrentState=EPBLauncherState::Ready,PendingState=EPBLauncherState::Ready;
    bool bIsFiring{},bPendingFiring{},bInProjectileControlMode{},bInSpecialMode{},activated{},deployType=true;
    APBCharacter* GetOwnerPawn()const{return pawn;}
    bool IsActivated()const{return activated;}
    bool IsA(int id)const override{return deployType && id==2;}
};
struct APBLauncher_Deploy:APBLauncher{static int StaticClass(){return 2;}};
struct APBVehicle : UObject { APBLauncher *LeftLauncher{}, *RightLauncher{}; };
struct APBCharacter:AActor
{
    APBVehicle pack; APBVehicle* CurrentVehicle=&pack;
    APlayerController* controller{};
    APBLauncher *CurrentLeftLauncher{},*CurrentRightLauncher{},*CurrentLauncher{};
    EPBHandsState CurrentHandsState=EPBHandsState::DeployedLauncherController;
    bool alive=true,killed{},formerlyLocal{};
    APlayerController* GetController()const{return controller;}
    bool IsAlive()const{return alive;}
    bool IsInKilledStatus()const{return killed;}
    bool GetIsLocalControlledBeforeDie()const{return formerlyLocal;}
};
struct UGameplayStatics
{
    static inline APlayerController* local{};
    static APlayerController* GetPlayerController(APBCharacter*,int){return local;}
};
}
#include "pod-view-entry.inc"
using namespace SDK;
struct PodMesh
{
    APBLauncher pod;
    AActor projectile;
    UPrimitiveComponent arm,mesh,ammo,thirdArm,thirdMesh,thirdChild,worldProjectile;
    USceneComponent connector;
    PodMesh(){
        for(auto p:{&arm,&mesh,&ammo,&thirdArm,&thirdMesh,&thirdChild})p->owner=&pod;
        connector.owner=&pod;worldProjectile.owner=&projectile;
        pod.ArmMesh1P=&arm;pod.Mesh1P=&mesh;pod.ArmMesh3P=&thirdArm;pod.Mesh3P=&thirdMesh;
        arm.children={&mesh,&thirdArm};mesh.children={&connector,&thirdMesh,&worldProjectile};connector.children={&ammo};
        thirdMesh.children={&thirdChild};
    }
};
struct Scene
{
    APBCharacter character,remote;
    APlayerController pc,other;
    PodMesh a,b;
    Scene(){
        pc.pawn=&character;character.controller=&pc;other.pawn=&remote;other.local=false;remote.controller=&other;
        a.pod.pawn=&character;b.pod.pawn=&character;character.pack.LeftLauncher=&a.pod;character.pack.RightLauncher=&b.pod;
        character.CurrentLauncher=&a.pod;UGameplayStatics::local=&pc;
    }
    void event(const char* name="Function Engine.Actor.ReceiveTick"){Entry(&character,name);}
    bool hidden(const UPrimitiveComponent& p,APlayerController* view=nullptr) {
        for(const auto& row:(view?view:&pc)->HiddenPrimitiveComponents)
            if(p.index==row.ObjectIndex && p.serial==row.ObjectSerialNumber)return true;
        return false;
    }
    void nativeHide(const UPrimitiveComponent& p){TWeakObjectPtr<UPrimitiveComponent> id{};id.ObjectIndex=p.index;id.ObjectSerialNumber=p.serial;pc.HiddenPrimitiveComponents.Add(id);}
    bool first(const PodMesh& pod,bool shouldHide){return hidden(pod.arm)==shouldHide && hidden(pod.mesh)==shouldHide && hidden(pod.ammo)==shouldHide;}
    bool world(const PodMesh& pod){return !hidden(pod.thirdArm)&&!hidden(pod.thirdMesh)&&!hidden(pod.thirdChild)&&!hidden(pod.worldProjectile)&&!hidden(pod.mesh,&other);}
};
struct Item{const char* id;bool deploy;};
int main()
{
    const Item catalog[]={
#include "pod-catalog.inc"
    };
    int failures=0;
    auto check=[&](std::string name,bool value){std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n';failures+=!value;};
    for(const auto& row:catalog){
        for(bool left:{true,false}){
            Scene s;
            s.character.pack.RightLauncher=left?nullptr:&s.a.pod;s.character.pack.LeftLauncher=left?&s.a.pod:nullptr;
            s.a.pod.deployType=row.deploy;s.event();
            bool ok=s.first(s.a,false)&&s.world(s.a);
            s.character.CurrentHandsState=EPBHandsState::DeployedWeapon;s.character.CurrentLauncher=nullptr;s.a.pod.CurrentState=EPBLauncherState::Standby;s.event();
            ok=ok&&s.first(s.a,true)&&s.world(s.a);
            if(row.deploy){s.character.CurrentHandsState=EPBHandsState::DeployingLauncherController;s.character.CurrentLauncher=&s.a.pod;s.a.pod.CurrentState=EPBLauncherState::Deploying;}
            else{s.a.pod.bPendingFiring=true;}
            s.event();ok=ok&&s.first(s.a,false)&&s.world(s.a);
            s.event("NotifyBeKilled");ok=ok&&s.first(s.a,true)&&s.world(s.a);
            check(std::string(row.id)+(left?" left":" right"),ok);
            Scene p;p.character.pack.RightLauncher=left?nullptr:&p.a.pod;p.character.pack.LeftLauncher=left?&p.a.pod:nullptr;
            p.a.pod.deployType=row.deploy;p.a.pod.pawn=nullptr;p.event();
            bool pending=p.first(p.a,true)&&p.world(p.a);
            p.a.pod.pawn=&p.character;p.event();
            check(std::string(row.id)+(left?" pending left":" pending right"),pending&&p.first(p.a,false)&&p.world(p.a));
        }
    }
    {Scene s;s.b.pod.pawn=nullptr;s.character.CurrentLauncher=&s.b.pod;s.event("NotifyBeKilled");bool closed=s.first(s.b,true);s.b.pod.pawn=&s.character;s.event();check("pending right stays parked after death and owner replication",closed&&s.first(s.b,true)&&s.world(s.b));}
    {Scene s;s.a.pod.pawn=nullptr;s.nativeHide(s.a.ammo);s.event();bool parked=s.first(s.a,true);s.character.pack.LeftLauncher=nullptr;s.event();check("lost pending slot releases only this view's additions",parked&&!s.hidden(s.a.arm)&&!s.hidden(s.a.mesh)&&s.hidden(s.a.ammo)&&s.world(s.a));}
    {Scene s;s.a.pod.pawn=nullptr;s.event();bool parked=s.first(s.a,true);s.a.pod.pawn=&s.remote;s.remote.pack.LeftLauncher=&s.a.pod;Entry(&s.remote,"ReceiveTick");check("new foreign owner releases provisional exclusion",parked&&s.first(s.a,false)&&s.world(s.a));}
    {Scene s;s.a.pod.pawn=nullptr;s.event();bool parked=s.first(s.a,true);s.pc.pawn=&s.remote;s.event("ReceiveUnpossessed");check("view change releases unconfirmed first person components",parked&&s.first(s.a,false)&&s.world(s.a));}
    {Scene s;s.a.pod.pawn=nullptr;s.pc.pawn=nullptr;s.character.controller=nullptr;s.character.formerlyLocal=true;s.event("NotifyBeKilled");check("formerly local flag cannot claim a new ownerless pod",s.first(s.a,false)&&s.world(s.a));}
    {Scene s;s.a.pod.pawn=&s.remote;s.event();check("contradictory owner beats stale local shoulder",s.first(s.a,false)&&s.world(s.a));}
    {Scene s;s.a.pod.pawn=nullptr;s.event();bool parked=s.first(s.a,true);s.character.CurrentLauncher=&s.b.pod;s.a.pod.pawn=&s.character;s.event();check("owner arriving after switch leaves old first person rig hidden",parked&&s.first(s.a,true)&&s.first(s.b,false)&&s.world(s.a));}
    {Scene s;s.event();s.character.CurrentLauncher=&s.b.pod;s.event();check("Sparrow and Probe use opposite shoulders",s.first(s.a,true)&&s.first(s.b,false)&&s.world(s.a)&&s.world(s.b));}
    {Scene s;s.nativeHide(s.a.ammo);s.character.CurrentLauncher=nullptr;s.event();s.character.CurrentLauncher=&s.a.pod;s.event();check("native camera exclusion stays present",s.hidden(s.a.ammo)&&!s.hidden(s.a.mesh));}
    {Scene s;s.a.mesh.authoredVisible=false;s.character.CurrentLauncher=nullptr;s.event();s.character.CurrentLauncher=&s.a.pod;s.event();check("authored mesh visibility preserved",!s.a.mesh.authoredVisible && !s.hidden(s.a.mesh));}
    {Scene s;s.character.CurrentLauncher=nullptr;s.event();s.a.ammo.owner=&s.a.projectile;s.event();check("projectile separates from parked pod",!s.hidden(s.a.ammo)&&s.hidden(s.a.mesh));}
    {Scene s;s.character.CurrentLauncher=nullptr;s.event();s.a.pod.pawn=&s.remote;s.remote.pack.LeftLauncher=&s.a.pod;Entry(&s.remote,"ReceiveTick");check("transferred equipment released from old view",s.first(s.a,false)&&s.world(s.a));}
    {Scene s;s.event();s.character.pack.LeftLauncher=nullptr;s.pc.pawn=nullptr;s.character.controller=nullptr;s.event("ReceiveUnpossessed");check("unpossess after native slots are cleared",s.first(s.a,true)&&s.world(s.a));}
    {Scene s;s.pc.pawn=nullptr;s.character.controller=nullptr;s.character.formerlyLocal=true;s.event("NotifyBeKilled");check("first event after local detachment",s.first(s.a,true)&&s.world(s.a));}
    {Scene s;s.event();s.event("NotifyBeKilled");s.event();bool kept=s.first(s.a,true);s.character.alive=false;s.event();s.character.alive=true;s.event();check("death before replication then revival",kept&&s.first(s.a,false));}
    {Scene s;s.event();s.a.pod.PendingState=EPBLauncherState::Undeploying;s.event();check("pending undeploy beats stale hands",s.first(s.a,true));}
    {Scene s;s.a.pod.deployType=false;s.character.CurrentLauncher=nullptr;s.a.pod.bInSpecialMode=true;s.event();check("active auxiliary mode while firing rifle",s.first(s.a,false));}
    {Scene s;s.character.CurrentLauncher=nullptr;s.a.pod.deployType=false;s.a.pod.CurrentState=EPBLauncherState::Ready;s.a.pod.activated=true;s.event();s.a.pod.activated=false;s.event();check("auxiliary ready state requires selection or activation",s.first(s.a,true));}
    {Scene s;s.remote.pack.LeftLauncher=&s.a.pod;s.a.pod.pawn=&s.remote;Entry(&s.remote,"ReceiveTick");check("unrelated remote owner leaves viewport intact",s.pc.HiddenPrimitiveComponents.Num()==0&&s.other.HiddenPrimitiveComponents.Num()==0);}
    {Scene s;s.character.CurrentLauncher=nullptr;s.event();const int count=s.pc.HiddenPrimitiveComponents.Num();s.event();s.event();check("repeated ticks add no duplicate exclusion",count==6&&s.pc.HiddenPrimitiveComponents.Num()==count);}
    {Scene s;s.a.mesh.children.push_back(&s.a.arm);s.character.CurrentLauncher=nullptr;s.event();check("two roots and cyclic test graph remain bounded",s.first(s.a,true)&&s.world(s.a));}
    {Scene s;s.character.CurrentLauncher=nullptr;s.event();++s.a.ammo.serial;s.event();s.character.CurrentLauncher=&s.a.pod;s.event();check("replacement component generation releases cleanly",!s.hidden(s.a.ammo)&&s.pc.HiddenPrimitiveComponents.Num()==3);}
    {
        Scene s;UPrimitiveComponent missiles[6];
        s.a.thirdArm.children={&s.a.arm,&s.a.thirdMesh};s.a.arm.children={&s.a.mesh};
        s.a.mesh.children.clear();s.a.thirdMesh.children.clear();
        for(int i=0;i<6;++i){missiles[i].owner=&s.a.pod;(i<3?s.a.mesh.children:s.a.thirdMesh.children).push_back(&missiles[i]);}
        s.character.CurrentLauncher=nullptr;s.event();
        bool matches=true;for(int i=0;i<6;++i)matches=matches&&(s.hidden(missiles[i])==(i<3));
        check("stock Sparrow keeps three exterior missiles",matches&&!s.hidden(s.a.thirdArm)&&!s.hidden(s.a.thirdMesh));
    }
    {
        Scene s;s.a.thirdArm.children={&s.a.arm,&s.a.thirdMesh};s.a.arm.children={&s.a.mesh};
        s.a.mesh.children.clear();s.a.thirdMesh.children.clear();s.character.pack.RightLauncher=nullptr;
        s.character.CurrentLauncher=nullptr;s.event();
        check("stock Probe keeps its separate world actor",s.pc.HiddenPrimitiveComponents.Num()==2&&s.hidden(s.a.arm)&&s.hidden(s.a.mesh)&&!s.hidden(s.a.worldProjectile));
    }
    return failures?1:0;
}
