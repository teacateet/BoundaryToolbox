#pragma once
// 小型编译夹具。仅模拟显式调用的 UE 接口，不代表 UE 编辑器或客户端。
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#define GENERATED_BODY()
#define PROJECTBOUNDARY_API
#define TEXT(x) x
using FName = std::string;
struct FVector {};
class FPrimitiveComponentId {
public:
    int Value = 0;
    bool operator<(const FPrimitiveComponentId& other) const { return Value < other.Value; }
};
template<class T> struct TSet : std::set<T> {
    void Add(const T& item) { this->insert(item); }
};
struct UPackage {
    FName Name;
    const FName& GetFName() const { return Name; }
};
struct UClass {
    FName Name;
    UPackage Package;
    const UClass* Super = nullptr;
    const FName& GetFName() const { return Name; }
    const UPackage* GetOutermost() const { return &Package; }
    const UClass* GetSuperClass() const { return Super; }
};
struct UObject {
    virtual ~UObject() = default;
    bool Valid = true;
    UClass* Class = nullptr;
    const UClass* GetClass() const { return Class; }
};
template<class T> bool IsValid(const T* ptr) { return ptr && ptr->Valid; }
template<class T, class U> auto Cast(U* ptr) {
    using Result = typename std::conditional<std::is_const_v<U>, const T*, T*>::type;
    return dynamic_cast<Result>(ptr);
}
template<class T> class TWeakObjectPtr {
    T* Raw = nullptr;
public:
    TWeakObjectPtr() = default;
    TWeakObjectPtr(T* raw) : Raw(raw) {}
    T* Get() const { return IsValid(Raw) ? Raw : nullptr; }
    bool operator<(const TWeakObjectPtr& other) const { return std::less<T*>{}(Raw, other.Raw); }
};
template<class K, class V> class TMap {
    using Data = std::map<K, V>;
    Data Items;
public:
    V& FindOrAdd(const K& key) { return Items[key]; }
    struct Iterator {
        Data& Items;
        typename Data::iterator At;
        bool Erased = false;
        explicit operator bool() const { return At != Items.end(); }
        const K& Key() const { return At->first; }
        V& Value() { return At->second; }
        void RemoveCurrent() { At = Items.erase(At); Erased = true; }
        void operator++() { if (!Erased) ++At; Erased = false; }
    };
    Iterator CreateIterator() { return {Items, Items.begin()}; }
};
enum class EVisibility { SelfHitTestInvisible, Collapsed };
class SWidget {
public:
    std::function<EVisibility()> Visibility = [] { return EVisibility::SelfHitTestInvisible; };
    std::shared_ptr<SWidget> Child;
};
template<class T> using TSharedRef = std::shared_ptr<T>;
struct SBoxBuilder {
    std::shared_ptr<SWidget> Box = std::make_shared<SWidget>();
    SBoxBuilder& Visibility_Lambda(std::function<EVisibility()> fn) {
        Box->Visibility = std::move(fn); return *this;
    }
    std::shared_ptr<SWidget> operator[](std::shared_ptr<SWidget> content) {
        Box->Child = std::move(content); return Box;
    }
};
#define SNew(Type) SBoxBuilder()
struct AActor : UObject {};
struct AController;
struct APawn : AActor {
    AController* Controller = nullptr;
    AController* GetController() const { return Controller; }
};
struct AController : AActor {
    APawn* Pawn = nullptr;
    APawn* GetPawn() const { return Pawn; }
};
struct APlayerController : AController {
    bool Local = true;
    bool IsLocalController() const { return Local; }
    virtual void UpdateHiddenComponents(const FVector&, TSet<FPrimitiveComponentId>& hidden) { hidden.Add({900}); }
    TSet<FPrimitiveComponentId> BuildView() {
        TSet<FPrimitiveComponentId> hidden;
        UpdateHiddenComponents({}, hidden);
        return hidden;
    }
};
struct USceneComponent : UObject {
    AActor* Owner = nullptr;
    std::vector<USceneComponent*> Attached;
    AActor* GetOwner() const { return Owner; }
    const auto& GetAttachChildren() const { return Attached; }
};
struct UPrimitiveComponent : USceneComponent {
    FPrimitiveComponentId ComponentId;
    bool Registered = true;
    bool IsRegistered() const { return Registered; }
};
struct USkeletalMeshComponent : UPrimitiveComponent {};
struct UStaticMeshComponent : UPrimitiveComponent {};
struct UUserWidget : UObject {
    APlayerController* Player = nullptr;
    bool Designer = false;
    int Invalidations = 0;
    std::shared_ptr<SWidget> Original = std::make_shared<SWidget>();
    APlayerController* GetOwningPlayer() const { return Player; }
    bool IsDesignTime() const { return Designer; }
    void InvalidateLayoutAndVolatility() { ++Invalidations; }
    virtual TSharedRef<SWidget> RebuildWidget() { return Original; }
    TSharedRef<SWidget> TakeWidget() { return RebuildWidget(); }
};
