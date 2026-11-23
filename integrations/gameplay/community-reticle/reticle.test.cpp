#include <cstdio>
#include <string>
#include <unordered_map>
namespace SDK {
struct UObject {
    static inline int next = 0;
    static inline std::unordered_map<int,UObject*> objects;
    int id = ++next, serial = 1;
    bool valid = true;
    UObject() { objects[id] = this; }
    virtual ~UObject() { valid = false; objects.erase(id); }
};
template<class T> struct TSoftObjectPtr { struct { int ObjectIndex, ObjectSerialNumber; } WeakPtr; };
struct UKismetSystemLibrary {
    static bool IsValid(const UObject* o) { return o && o->valid; }
    static TSoftObjectPtr<UObject> Conv_ObjectToSoftObjectReference(UObject* o) { return {{o->id,o->serial}}; }
    static UObject* Conv_SoftObjectReferenceToObject(const TSoftObjectPtr<UObject>& key) {
        auto it = UObject::objects.find(key.WeakPtr.ObjectIndex);
        return it == UObject::objects.end() ? nullptr : it->second;
    }
};
enum class EPBHandsState { DeployedWeapon, DeployingLauncherController, DeployedLauncherController, UndeployingLauncherController };
enum class EPBLauncherState { Standby, Ready, Undeploying, Deploying };
struct APBCharacter; struct APBLauncher;
struct AController : UObject { bool local = true; bool IsLocalPlayerController() const { return local; } };
struct APlayerController : AController { APBCharacter* pawn = nullptr; APBCharacter* K2_GetPawn() const { return pawn; } };
struct UCanvasPanel : UObject {
    float opacity = 1.0f;
    int nativeVisibility = 3, writes = 0;
    float GetRenderOpacity() const { return opacity; }
    void SetRenderOpacity(float v) { opacity = v; ++writes; }
};
struct UPBLauncherWidget : UObject {
    APBLauncher* OwningLauncher = nullptr;
    APlayerController* controller = nullptr;
    bool screen = true;
    float opacity = 1.0f;
    virtual bool IsA(int) const { return false; }
    APlayerController* GetOwningPlayer() const { return controller; }
};
struct UUMG_CrossHair_Missile_C : UPBLauncherWidget {
    UCanvasPanel* CanvasPanel_0 = nullptr;
    static int StaticClass() { return 7; }
    bool IsA(int classId) const override { return classId == StaticClass(); }
};
struct APBLauncher : UObject {
    UPBLauncherWidget* CrossHairWidget = nullptr;
    APBCharacter* pawn = nullptr;
    EPBLauncherState CurrentState = EPBLauncherState::Ready, PendingState = EPBLauncherState::Ready;
    APBCharacter* GetOwnerPawn() const { return pawn; }
};
struct APBVehicle : UObject { APBLauncher *LeftLauncher{}, *RightLauncher{}; };
struct APBCharacter : UObject {
    APBVehicle pack; APBVehicle* CurrentVehicle=&pack;
    APlayerController* controller = nullptr;
    APBLauncher* CurrentLeftLauncher = nullptr; APBLauncher* CurrentRightLauncher = nullptr; APBLauncher* CurrentLauncher = nullptr;
    bool living = true, dead = false, formerlyLocal = false;
    EPBHandsState CurrentHandsState = EPBHandsState::DeployedLauncherController;
    APlayerController* GetController() const { return controller; }
    bool IsAlive() const { return living; }
    bool IsInKilledStatus() const { return dead; }
    bool GetIsLocalControlledBeforeDie() const { return formerlyLocal; }
};
}
using namespace SDK;

// 使用提供的社区源文件中现有角色回调的入口，而非在测试中主动调用修复。
#include "reticle-entry.inc"
struct View {
    APlayerController pc; APBCharacter owner; APBLauncher left, right; UCanvasPanel canvas; UUMG_CrossHair_Missile_C widget;
    View() {
        owner.controller=&pc; pc.pawn=&owner; left.pawn=right.pawn=&owner;
        owner.pack.LeftLauncher=owner.CurrentLauncher=&left; owner.pack.RightLauncher=&right;
        left.CrossHairWidget=&widget; widget.OwningLauncher=&left; widget.controller=&pc; widget.CanvasPanel_0=&canvas;
    }
    void tick() { ReticleCallback(&owner,"Function Engine.Actor.ReceiveTick"); }
    void event(const char* n) { ReticleCallback(&owner,n); }
};
int main() {
    int failed=0;
    auto check=[&](const char* name,bool ok){ std::printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)++failed; };
    {
        View v; v.canvas.opacity=0.65f; v.tick();
        check("active reticle preserves authored opacity",v.canvas.opacity==0.65f && !v.canvas.writes);
        v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon; v.tick();
        check("switch to rifle with stale selected pod",v.canvas.opacity==0);
        int first = v.canvas.writes; v.tick(); check("stowed repeated tick has no additional write",v.canvas.writes==first);
        check("authored widget visibility and parent opacity remain",v.canvas.nativeVisibility==3 && v.widget.opacity==1.0f && v.widget.screen);
        v.owner.CurrentHandsState=EPBHandsState::DeployingLauncherController; v.tick();
        check("re-equip recovers exact former opacity",v.canvas.opacity==0.65f);
        v.owner.CurrentLauncher=&v.right; v.tick(); check("other shoulder wins selection",v.canvas.opacity==0);
        v.owner.CurrentLauncher=&v.left;v.tick();
        v.left.PendingState=EPBLauncherState::Undeploying;v.tick();check("pending undeploy hides before hands update",v.canvas.opacity==0);
        v.left.PendingState=EPBLauncherState::Ready;v.tick();
        v.event("Function ProjectBoundary.PBCharacter.NotifyBeKilled");check("death event before replicated state",v.canvas.opacity==0);
        v.tick();check("tick cannot reopen a dead life before replication",v.canvas.opacity==0);
        v.owner.living=false; v.owner.dead=true;v.tick();
        v.owner.living=true;v.owner.dead=false;v.tick();check("revived same pawn recovers its original opacity",v.canvas.opacity==0.65f);
        v.owner.controller=nullptr;v.pc.pawn=nullptr;v.owner.pack.LeftLauncher=nullptr;v.owner.CurrentLauncher=nullptr;
        v.event("Function Engine.Pawn.ReceiveUnpossessed");check("unpossess after inventory is cleared",v.canvas.opacity==0);
        v.owner.controller=&v.pc;v.pc.pawn=&v.owner;v.owner.pack.RightLauncher=v.owner.CurrentLauncher=&v.left;
        v.event("Function Engine.Pawn.ReceivePossessed");check("right slot re-possession",v.canvas.opacity==0.65f);
        v.event("Function Engine.Actor.ReceiveEndPlay");check("end play with visible reticle",v.canvas.opacity==0);
    }
    {
        View v, other; v.tick(); other.tick();
        v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();
        check("other local player never loses reticle",other.canvas.opacity==1 && !other.canvas.writes);
        v.widget.controller=&other.pc;v.tick();check("transferred viewport restores old adjustment",v.canvas.opacity==1);
        other.pc.local=false;other.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;other.tick();
        check("remote pawn does not own a newly observed widget",other.canvas.opacity==1);
    }
    {
        View v; v.widget.controller=nullptr;v.tick();
        v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();
        check("local pod owns widget without explicit UMG viewer",v.canvas.opacity==0);
        UUMG_CrossHair_Missile_C identicalClassSidebar; UCanvasPanel sidebar; identicalClassSidebar.OwningLauncher=&v.left;identicalClassSidebar.CanvasPanel_0=&sidebar;
        check("same class sidebar outside launcher reticle stays visible",sidebar.opacity==1);
        v.canvas.opacity=0.25f;v.tick();check("game changes authored inner opacity while parked",v.canvas.opacity==0);
        v.owner.CurrentHandsState=EPBHandsState::DeployedLauncherController;v.tick();check("latest authored opacity recovered",v.canvas.opacity==0.25f);
        v.canvas.opacity=0;v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();
        v.owner.CurrentHandsState=EPBHandsState::DeployedLauncherController;v.tick();check("originally transparent remains transparent",v.canvas.opacity==0);
    }
    {
        View v;v.tick();UUMG_CrossHair_Missile_C replacement;UCanvasPanel newCanvas;replacement.CanvasPanel_0=&newCanvas;replacement.controller=&v.pc;replacement.OwningLauncher=&v.left;
        v.left.CrossHairWidget=&replacement;v.tick();check("old widget hidden when replaced",v.canvas.opacity==0 && newCanvas.opacity==1);
        v.widget.valid=false;v.canvas.valid=false;v.tick();
        newCanvas.opacity=0.75f;v.owner.pack.LeftLauncher=nullptr;v.owner.CurrentLauncher=nullptr;v.tick();check("lost slot retires retained local reticle",newCanvas.opacity==0);
        newCanvas.serial++;newCanvas.opacity=0.4f;v.tick();check("reused object storage is never authorized by old entry",newCanvas.opacity==0.4f);
    }
    {
        View v;v.tick();v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();
        UCanvasPanel replaced;replaced.opacity=0.3f;v.widget.CanvasPanel_0=&replaced;v.tick();
        check("former root restores on canvas rebuild",v.canvas.opacity==1 && replaced.opacity==0);
        v.owner.CurrentHandsState=EPBHandsState::DeployedLauncherController;v.tick();check("rebuilt canvas remembers its own opacity",replaced.opacity==0.3f);
        UPBLauncherWidget plain;plain.controller=&v.pc;plain.OwningLauncher=&v.left;v.left.CrossHairWidget=&plain;v.tick();
        check("other reticle class retains own visibility",plain.screen && plain.opacity==1);
    }
    {
        View v;v.owner.controller=nullptr;v.pc.pawn=nullptr;v.owner.formerlyLocal=true;
        v.event("Function Engine.Actor.ReceiveEndPlay");
        check("first event after detachment uses confirmed former local owner",v.canvas.opacity==0);
        View remote;remote.pc.local=false;remote.owner.formerlyLocal=false;remote.owner.controller=nullptr;remote.pc.pawn=nullptr;
        remote.event("Function Engine.Actor.ReceiveEndPlay");
        check("foreign first death does not authorize a new widget",remote.canvas.writes==0);
    }
    {
        View v;v.canvas.opacity=0.65f;v.left.pawn=nullptr;
        v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();
        check("first frame local slot hides reticle while pod owner is pending",v.canvas.opacity==0);
        v.left.pawn=&v.owner;v.owner.CurrentHandsState=EPBHandsState::DeployedLauncherController;v.tick();
        check("confirmed deployment restores first frame authored opacity",v.canvas.opacity==0.65f);
    }
    {
        View v;v.left.pawn=nullptr;v.owner.pack.LeftLauncher=nullptr;v.owner.pack.RightLauncher=&v.left;
        v.event("Function ProjectBoundary.PBCharacter.NotifyBeKilled");
        check("first death hides pending right slot reticle",v.canvas.opacity==0);
        v.left.pawn=&v.owner;v.tick();check("owner arriving after death leaves reticle hidden",v.canvas.opacity==0);
    }
    {
        View v,other;v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;
        v.left.pawn=nullptr;v.widget.controller=nullptr;v.tick();
        check("missing both pod and widget owners leaves visibility alone",v.canvas.writes==0);
        v.widget.controller=&other.pc;v.tick();
        check("pending pod with another local viewport remains untouched",v.canvas.writes==0);
        v.widget.controller=&v.pc;v.left.pawn=&other.owner;v.tick();
        check("foreign pod in a local slot remains untouched",v.canvas.writes==0);
    }
    {
        View v;v.canvas.opacity=0.4f;v.left.pawn=nullptr;
        v.owner.CurrentHandsState=EPBHandsState::DeployedWeapon;v.tick();v.widget.controller=nullptr;v.tick();
        check("unconfirmed reticle losing viewport releases the adjustment",v.canvas.opacity==0.4f);
    }
    return failed?1:0;
}
