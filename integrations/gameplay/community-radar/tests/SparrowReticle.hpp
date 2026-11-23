#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace SDK {
struct UObject {
    static inline int next = 0;
    static inline std::unordered_map<int, UObject*> objects;
    int id = ++next, serial = 1;
    bool valid = true;
    UObject() { objects[id] = this; }
    UObject(const UObject&) = delete;
    virtual ~UObject() { objects.erase(id); }
    virtual bool IsA(int) const { return false; }
};
template<class T> struct TArray : std::vector<T> { using std::vector<T>::vector; int Num() const { return int(this->size()); } };
template<class K, class V> struct TMap {
    // 以相同顺序枚举引擎的键和值。
    struct Pair { K k; V v; const K& Key() const { return k; } const V& Value() const { return v; } };
    std::vector<Pair> pairs;
    auto begin() const { return pairs.begin(); } auto end() const { return pairs.end(); }
    void Put(K k, V v) { pairs.push_back({k,v}); }
    void Reset() { pairs.clear(); }
};
struct FVector { float X=0, Y=0, Z=0; };
struct FVector2D { float X=0,Y=0; };
struct FString { explicit FString(const wchar_t*) {} std::wstring ToWString() const { return {}; } };
struct APBPlayerState : UObject { int team{}; int GetTeamId() const {return team;} };
struct APBCharacter;
struct APlayerController : UObject {
    APBPlayerState* PlayerState{}; bool inside=true;
    void GetViewportSize(int* w,int* h){*w=960; *h=540;}
    bool ProjectWorldLocationToScreen(FVector,FVector2D* screen,bool){*screen={100,100};return inside;}
    bool local=true; APBCharacter* pawn=nullptr;
    bool IsLocalPlayerController() const { return local; } APBCharacter* K2_GetPawn() const { return pawn; }
};
struct APBCharacter : UObject {
    static int StaticClass(){return 108;} bool IsA(int cls) const override {return cls==108;} int TeamID{};
    bool alive=true,killed=false,radar=true; APlayerController* controller=nullptr; FVector at{};
    APlayerController* GetController() const { return controller; }
    bool IsAlive() const { return alive; } bool IsInKilledStatus() const { return killed; }
    bool IsInNormalRadarSignal() const { return radar; } FVector K2_GetActorLocation() const { return at; }
};
struct UWidget : UObject {
    bool visible=true, viewport=false; float opacity=1; FVector2D size{50,50};
    UWidget* parent=nullptr; APlayerController* owner=nullptr;
    UWidget* GetParent() const { return parent; } bool IsInViewport() const { return viewport; }
    bool IsVisible() const { return visible; } float GetRenderOpacity() const { return opacity; }
    APlayerController* GetOwningPlayer() const { return owner; } FVector2D GetCachedGeometry() const { return size; }
    void RemoveFromParent() { parent=nullptr; }
};
struct UUserWidget : UWidget {};
struct UCanvasPanel : UWidget {};
struct UTexture2D : UObject {};
enum class EPBCamp { Enemy, Ally };
enum class EPBMarkableType { Character, Projectile };
enum class EPBIndicatorStatus { InCircle, OnScreenAndOutOfCircle, Outside };
enum class EPBMarkedStatus { SensorMark, ReconMark, VisionMark, RadarMark, None };
struct UPBMarkableComponent : UObject {
    EPBMarkableType MarkableType=EPBMarkableType::Character; UTexture2D* MiniMapTexture=nullptr;
    bool clear=true; bool IsNotBlocked() const { return clear; }
};
struct UPBIndicator : UUserWidget {
    UPBMarkableComponent* RelatedMarkable=nullptr; EPBCamp Camp=EPBCamp::Enemy;
    bool bIsDying=false,bMarkableIsInView=true; EPBIndicatorStatus Status=EPBIndicatorStatus::InCircle;
    EPBMarkedStatus MarkedStatus=EPBMarkedStatus::VisionMark; FVector WorldLocation{};
};
struct UPBDynamicMarkableComponent : UPBMarkableComponent {
    bool bMarked{}, bSharedMarked{}, bMarkedBySensor{}; APBCharacter* owner{};
    TArray<APBPlayerState*> MarkCounterArray, SharedMarkCounterArray, SensorMarkCounterArray;
    APBCharacter* GetOwner(){return owner;}
};
struct FPBMiniMapIconInfo {
    FVector WorldLocation{}; FVector2D position{}; UTexture2D* Texture=nullptr;
    EPBCamp Camp=EPBCamp::Enemy; EPBMarkableType MarkableType=EPBMarkableType::Character;
    bool bMarked=false,bIsLocal=false;
};
struct UUMG_MinimapNode_C : UUserWidget {
    FPBMiniMapIconInfo MiniMapIconInfo{}; int updates=0;
    static int StaticClass() { return 100; } bool IsA(int k) const override { return k==StaticClass(); }
    void UpdateNode(const FPBMiniMapIconInfo& info) { MiniMapIconInfo=info; ++updates; }
};
struct UUMG_HelmetHUD_MiniMap_C : UUserWidget {
    APBCharacter* OnwerCharacter=nullptr; UCanvasPanel* MiniMapNodeContainer=nullptr;
    float MiniMapWorldHalfRange=1000; FVector2D IconPositionUVClamp_U{0,1},IconPositionUVClamp_V{0,1};
    TMap<UPBMarkableComponent*,FPBMiniMapIconInfo> MapNodeInfo,LastCaptureInfoNodeMap;
    TArray<UUserWidget*> DrewMap,NotUsedWidgetArray; int UPBMiniMapNodeClass=UUMG_MinimapNode_C::StaticClass(); bool attach=true;
    static int StaticClass() { return 101; } bool IsA(int k) const override { return k==StaticClass(); }
    FVector2D Get2DPositionByLocation(FVector at) const { return {0.5f+at.X/2000,0.5f+at.Y/2000}; }
    void K2_AddWidgetToCanvasPanel(UUMG_MinimapNode_C* widget) { if(attach)widget->parent=MiniMapNodeContainer; }
};
struct UPBMarkerManager : UObject {
    TArray<UPBDynamicMarkableComponent*> MarkedArray, SharedMarkedArray, SensorMarkedArray;
    static int StaticClass() { return 102; }
    TMap<UPBMarkableComponent*,UPBIndicator*> IndicatorMap,VisionMarkedMap,ForceMarkedMap;
};
struct GameMode { std::string name="BP_PBGameMode_Rush_PVE_Normal_C"; std::string GetName() const { return name; } };
struct ModeRef { GameMode* mode=nullptr; GameMode* Get() const { return mode; } GameMode* operator->() const { return mode; } };
struct APBGameState : UObject {
    ModeRef GameModeClass; static int StaticClass() { return 103; } bool IsA(int k) const override { return k==StaticClass(); }
};
struct UKismetSystemLibrary {
    static FString GetProjectSavedDirectory() { return FString(L""); }
    static inline bool requested=true;
    static bool IsValid(const UObject* p) { return p && p->valid; }
    static int GetCommandLine() { return 0; } static bool ParseParam(int,const FString&) {return requested; }
};
struct USlateBlueprintLibrary { static FVector2D GetLocalSize(FVector2D size) { return size; } };
struct UGameplayStatics {
    static inline APBGameState* state=nullptr;
    static APBGameState* GetGameState(APBCharacter*) { return state; }
};
struct USubsystemBlueprintLibrary {
    static inline UPBMarkerManager* manager=nullptr;
    static UPBMarkerManager* GetWorldSubsystem(APBCharacter*,int) { return manager; }
};
struct UWidgetBlueprintLibrary {
    static inline TArray<UUserWidget*> available;
    static inline std::vector<std::unique_ptr<UUMG_MinimapNode_C>> made;
    static void GetAllWidgetsOfClass(APBCharacter*,TArray<UUserWidget*>* out,int cls,bool) {
        for(auto* w:available)if(w->IsA(cls))out->push_back(w);
    }
    static UUserWidget* Create(UUserWidget*,int cls,APlayerController* pc) {
        if(cls!=UUMG_MinimapNode_C::StaticClass())return nullptr;
        auto& dot=made.emplace_back(std::make_unique<UUMG_MinimapNode_C>());dot->owner=pc;return dot.get();
    }
};
}
namespace BrtSparrowReticle {
struct Instance {
    int id,serial;
    explicit Instance(SDK::UObject* obj):id(obj->id),serial(obj->serial){}
    SDK::UObject* Resolve() const {
        auto it=SDK::UObject::objects.find(id);
        return it!=SDK::UObject::objects.end() && it->second->valid && it->second->serial==serial ? it->second : nullptr;
    }
};
inline bool Event(std::string_view name,std::string_view leaf) {
    return name==leaf || (name.size()>leaf.size() && name[name.size()-leaf.size()-1]=='.' && name.ends_with(leaf));
}
}
