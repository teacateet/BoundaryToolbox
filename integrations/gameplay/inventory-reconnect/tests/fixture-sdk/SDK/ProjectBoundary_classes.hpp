#pragma once
namespace SDK {
struct UObject { int index, serial; bool valid=true; };
template<class T> struct TSoftObjectPtr {
    struct Key {int ObjectIndex,ObjectSerialNumber;} WeakPtr; T* object;
};
struct UKismetSystemLibrary {
    static bool IsValid(const UObject* p){return p && p->valid;}
    static TSoftObjectPtr<UObject> Conv_ObjectToSoftObjectReference(UObject* p){return {{p->index,p->serial},p};}
    static UObject* Conv_SoftObjectReferenceToObject(const TSoftObjectPtr<UObject>& p){return p.object;}
};
struct APBPlayerController;
struct UNetConnection : UObject { APBPlayerController* PlayerController=nullptr; };
struct AGameMode : UObject { bool IsActorBeingDestroyed() const {return false;} };
struct APBPlayerState : UObject {
    bool selected=false;
    static int StaticClass(){return 2;}
    bool IsA(int k) const {return k==StaticClass();}
    bool HasSelectedRole() const {return selected;}
};
struct APBPlayerController : UObject {
    UNetConnection* NetConnection=nullptr;bool destroying=false;APBPlayerState* PlayerState=nullptr;
    bool HasAuthority() const {return true;}
    bool IsActorBeingDestroyed() const{return destroying;}
};
}
