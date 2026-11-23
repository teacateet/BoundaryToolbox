#include <ProjectTypes.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static int Passed = 0;
void CheckAllPods();
static void Check(const char* name, bool result)
{
    if (!result) { std::fprintf(stderr, "FAILED %s\n", name); std::exit(1); }
    std::printf("ok %s\n", name);
    ++Passed;
}

static bool HiddenEquals(const TSet<FPrimitiveComponentId>& view, std::initializer_list<int> expected)
{
    bool equal = view.size() == expected.size();
    for (int item : expected) equal = equal && view.count({item});
    if (!equal)
    {
        std::fprintf(stderr,"view");
        for (auto id : view) std::fprintf(stderr," %d", id.Value);
        std::fprintf(stderr," expected");
        for (int id : expected) std::fprintf(stderr," %d", id);
        std::fprintf(stderr,"\n");
    }
    return equal;
}

int main()
{
    UClass missileType{"APBHomingMissileLauncher_BP_C", {"/Game/Blueprints/PackModules/Missile/APBHomingMissileLauncher_BP"}};
    UClass sensorType{"PBSensorLauncher_BP_C", {"/Game/Blueprints/PackModules/Sensor/PBSensorLauncher_BP"}};
    UClass scopeType{"UMG_CrossHair_Missile_C", {"/Game/UI/Launcher/CrossHair/UMG_CrossHair_Missile"}};
    UClass probeScopeType{"UMG_CrossHair_Sensor_C", {"/Game/UI/Launcher/CrossHair/UMG_CrossHair_Sensor"}};
    UClass radarType{"UMG_HelmetHUD_EnemyLockOn_C", {"/Game/UI/Styles/HelmetHUD/EnemyLockOn"}};
    UClass otherPackage{"UMG_CrossHair_Missile_C", {"/Game/Test/OtherWidget"}};
    UClass derivedSensor{"ProbeVariant_C", {"/Game/Test/ProbeVariant"}, &sensorType};
    APBPlayerController pc, remote;
    APBCharacter character, otherCharacter;
    pc.Pawn = &character; character.Controller = &pc;
    remote.Pawn = &otherCharacter; otherCharacter.Controller = &remote; remote.Local = false;
    APBLauncher_Deploy missile, sensor;
    character.CurrentLeftLauncher = &missile; character.CurrentRightLauncher = &sensor;
    missile.Owner = &character; missile.Class = &missileType;
    sensor.Owner = &character; sensor.Class = &derivedSensor;
    UPBLauncherWidget scope, probeScope, radar, extra;
    scope.OwningLauncher = &missile; scope.Player = &pc; scope.Class = &scopeType;
    probeScope.OwningLauncher = &sensor; probeScope.Player = &pc; probeScope.Class = &probeScopeType;
    radar.Class = &radarType; extra.Class = &otherPackage;
    missile.CrossHairWidget = &scope; sensor.CrossHairWidget = &probeScope;
    auto m = scope.TakeWidget(), s = probeScope.TakeWidget();
    auto showing = [](const TSharedRef<SWidget>& widget) { return widget->Visibility() != EVisibility::Collapsed; };
    Check("real target wraps original widget and preserves pre-existing override", m->Child == scope.Original && scope.OriginalRebuilds == 1);
    const auto warning = radar.TakeWidget(), sameName = extra.TakeWidget();
    Check("red warning and unowned same-name widget preserve original content and native visibility",
          warning->Child == radar.Original && sameName->Child == extra.Original && showing(warning) && showing(sameName));
    USkeletalMeshComponent arm, third;
    UStaticMeshComponent sensorMesh, missileMesh, launched, child, thirdChild, unregistered;
    AActor flyingProbe;
    arm.Owner = &sensor; arm.ComponentId = {10}; sensor.ArmMesh1P = &arm;
    sensorMesh.Owner = &sensor; sensorMesh.ComponentId = {11}; sensor.Mesh1P = &sensorMesh;
    child.Owner = &sensor; child.ComponentId = {12};
    third.Owner = &sensor; third.ComponentId = {13}; sensor.ArmMesh3P = &third;
    thirdChild.Owner = &sensor; thirdChild.ComponentId = {14}; third.Attached = {&thirdChild};
    launched.Owner = &flyingProbe; launched.ComponentId = {15};
    unregistered.Owner = &sensor; unregistered.ComponentId = {16}; unregistered.Registered = false;
    arm.Attached = {&child, &third, &launched, &unregistered};
    missileMesh.Owner = &missile; missileMesh.ComponentId = {20}; missile.Mesh1P = &missileMesh;
    Check("holstered two pods hide local geometry while preserving third-person, deployed actor and original filter",
          !showing(m) && !showing(s) && HiddenEquals(pc.BuildView(), {10,11,12,20,900}));
    character.CurrentLauncher = &missile; character.CurrentHandsState = EPBHandsState::DeployingLauncherController;
    auto deploying = pc.BuildView();
    Check("deploy Sparrow shows own ring, keeps probe stowed", showing(m) && !showing(s) && HiddenEquals(deploying,{10,11,12,900}));
    character.CurrentHandsState = EPBHandsState::FixingAirTightness;
    Check("repair closes ring and former first-person model", !showing(m) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    character.CurrentHandsState = EPBHandsState::DeployedWeapon;
    Check("DMR or FOP with stale CurrentLauncher still close ring and model", !showing(m) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    character.CurrentLauncher = &sensor; character.CurrentHandsState = EPBHandsState::DeployedLauncherController;
    Check("switch to probe releases only its reticle and first-person model",
          !showing(m) && showing(s) && HiddenEquals(pc.BuildView(),{20,900}));
    sensor.Undeploy = true;
    Check("undeploy closes before hands replication catches up",
          !showing(s) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    sensor.Undeploy = false; sensor.Standby = true;
    Check("standby hides independently of hand state", !showing(s) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    sensor.Standby = false;
    probeScope.Player = nullptr;
    Check("widget created before local player assignment uses owned launcher pawn", showing(s));
    probeScope.Player = &remote;
    Check("remote controller cannot show local overlay", !showing(s) && HiddenEquals(remote.BuildView(),{900}));
    probeScope.Player = &pc;
    auto active = pc.BuildView();
    int before = probeScope.Invalidations;
    pc.BuildView();
    Check("unchanged active reticle does not churn cached layout", before > 0 && probeScope.Invalidations == before);
    character.Living = false;
    Check("final death closes both stale reticles and first-person meshes", !showing(s) && !showing(m) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    pc.Pawn = nullptr;
    Check("death camera without a pawn still hides previous first-person models", !showing(s) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    APBCharacter replacement;
    pc.Pawn = &replacement; replacement.Controller = &pc;
    replacement.CurrentRightLauncher = &sensor; replacement.CurrentLauncher = &sensor;
    replacement.CurrentHandsState = EPBHandsState::DeployedLauncherController;
    Check("new life cannot inherit dead character reticle", !showing(s) && HiddenEquals(pc.BuildView(),{10,11,12,20,900}));
    sensor.Owner = &replacement;
    Check("new owner's own re-equipped probe wakes next view", showing(s) && HiddenEquals(pc.BuildView(),{20,900}));
    missile.Owner = &otherCharacter;
    Check("former local pod transferred to another player is released", HiddenEquals(pc.BuildView(),{900}));
    sensor.Valid = false;
    Check("destroyed launcher invalidates surviving widget and drops weak handle", !showing(s) && HiddenEquals(pc.BuildView(),{900}));
    CheckAllPods();
    std::printf("passed=%d\n", Passed);
}
