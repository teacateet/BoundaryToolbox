#pragma once
// 开发用局内探针。只在构建加 -IncludeDevRig、进程带 -BrtRig 时工作，不进入玩家包。
// 从 logs/BRT-rig-<side>.cmd 逐行读命令，结果追加到 logs/BRT-rig-<side>.out。
#include "../SDK.hpp"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <list>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace brt::rig {
using namespace SDK;

inline bool Enabled() {
    static const bool on = std::string(GetCommandLineA()).find("-BrtRig") != std::string::npos;
    return on;
}

struct Watch { std::string token; int index; FName name; std::string props, last; };
struct State {
    const char* side = "client";
    std::ofstream out;
    size_t done = 0;
    DWORD thread = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), next{};
    std::vector<std::string> traces, mutes, stacks;
    std::vector<Watch> watches;
    std::list<std::wstring> strings;
    unsigned traceRows = 0;
};
inline State state;

inline long long Ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state.start).count();
}
inline void Out(const char* format, ...) {
    char text[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (!state.out.is_open()) return;
    state.out << Ms() << ' ' << text << '\n';
    state.out.flush();
}

inline bool Has(std::string text, std::string part) {
    auto lower = [](std::string& s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); };
    lower(text); lower(part);
    return text.find(part) != std::string::npos;
}
inline UObject* At(int index) {
    if (index < 0 || index >= UObject::GObjects->Num()) return nullptr;
    return UObject::GObjects->GetByIndex(index);
}
inline std::string Ref(const UObject* object) {
    if (!object) return "null";
    return "#" + std::to_string(object->Index) + ":" + object->GetName();
}
inline bool ClassHas(const UObject* object, const std::string& part) {
    for (const UStruct* s = object->Class; s; s = s->Super) if (Has(s->GetName(), part)) return true;
    return false;
}

inline APlayerController* LocalController() {
    auto* world = UWorld::GetWorld();
    if (!world) return nullptr;
    if (state.side[0] == 's') return UGameplayStatics::GetPlayerController(world, 0);
    if (!world->OwningGameInstance || world->OwningGameInstance->LocalPlayers.Num() == 0) return nullptr;
    auto* player = world->OwningGameInstance->LocalPlayers[0];
    return player ? player->PlayerController : nullptr;
}

inline UObject* Find(const std::string& token) {
    if (token.empty()) return nullptr;
    if (token[0] == '#') return At(std::atoi(token.c_str() + 1));
    if (token.rfind("cdo:", 0) == 0) {
        for (int i = 0; i < UObject::GObjects->Num(); ++i) {
            auto* object = At(i);
            if (object && object->IsDefaultObject() && Has(object->GetName(), token.substr(4))) return object;
        }
        return nullptr;
    }
    auto* world = UWorld::GetWorld();
    auto* pc = LocalController();
    if (token == "world") return world;
    if (token == "pc") return pc;
    if (token == "pawn") return pc ? pc->K2_GetPawn() : nullptr;
    if (token == "ps") return pc ? pc->PlayerState : nullptr;
    if (token == "hud") return pc ? pc->MyHUD : nullptr;
    if (token == "gm") return world ? world->AuthorityGameMode : nullptr;
    if (token == "gs") return world ? world->GameState : nullptr;
    for (int i = 0; i < UObject::GObjects->Num(); ++i) {
        auto* object = At(i);
        if (object && !object->IsDefaultObject() && Has(object->GetFullName(), token)) return object;
    }
    return nullptr;
}

inline std::string Value(const FProperty* p, const std::uint8_t* at, int depth);
inline std::string Fields(const UStruct* type, const std::uint8_t* base, int depth, const std::string& filter) {
    std::string text;
    for (const UStruct* s = type; s; s = s->Super)
        for (const FField* f = s->ChildProperties; f; f = f->Next) {
            const auto name = f->Name.ToString();
            if (!filter.empty() && !Has(name, filter)) continue;
            const auto* p = static_cast<const FProperty*>(f);
            text += name + "=" + Value(p, base + p->Offset, depth) + (depth ? "," : "\n");
        }
    return text;
}
inline std::string Value(const FProperty* p, const std::uint8_t* at, int depth) {
    const auto type = p->ClassPrivate->Name.ToString();
    char number[64]{};
    if (type == "BoolProperty") {
        const auto* b = static_cast<const FBoolProperty*>(p);
        return (at[b->ByteOffset] & b->FieldMask) ? "1" : "0";
    }
    if (type == "IntProperty" || type == "UInt32Property") return std::to_string(*reinterpret_cast<const std::int32_t*>(at));
    if (type == "Int64Property" || type == "UInt64Property") return std::to_string(*reinterpret_cast<const std::int64_t*>(at));
    if (type == "Int16Property" || type == "UInt16Property") return std::to_string(*reinterpret_cast<const std::int16_t*>(at));
    if (type == "FloatProperty") { std::snprintf(number, sizeof(number), "%.3f", *reinterpret_cast<const float*>(at)); return number; }
    if (type == "DoubleProperty") { std::snprintf(number, sizeof(number), "%.3f", *reinterpret_cast<const double*>(at)); return number; }
    if (type == "ByteProperty" || type == "Int8Property" || (type == "EnumProperty" && p->ElementSize == 1)) return std::to_string(*at);
    if (type == "EnumProperty") return std::to_string(*reinterpret_cast<const std::int32_t*>(at));
    if (type == "NameProperty") return reinterpret_cast<const FName*>(at)->ToString();
    if (type == "StrProperty") return "\"" + reinterpret_cast<const FString*>(at)->ToString() + "\"";
    if (type == "ObjectProperty" || type == "ClassProperty" || type == "InterfaceProperty")
        return Ref(*reinterpret_cast<UObject* const*>(at));
    if (type == "WeakObjectProperty") {
        const auto* weak = reinterpret_cast<const FWeakObjectPtr*>(at);
        return "weak:" + Ref(weak->ObjectIndex > 0 ? At(weak->ObjectIndex) : nullptr);
    }
    if (type == "SoftObjectProperty" || type == "SoftClassProperty") return "soft:" + reinterpret_cast<const FName*>(at + 8)->ToString();
    if (type == "StructProperty") {
        const auto* s = static_cast<const FStructProperty*>(p);
        if (depth >= 3) return "{" + s->Struct->GetName() + "}";
        return "{" + Fields(s->Struct, at, depth + 1, "") + "}";
    }
    if (type == "ArrayProperty") {
        const auto* a = static_cast<const FArrayProperty*>(p);
        const auto* data = *reinterpret_cast<const std::uint8_t* const*>(at);
        const int count = *reinterpret_cast<const std::int32_t*>(at + 8);
        std::string text = "[" + std::to_string(count) + "]";
        if (!data || count <= 0 || count > 100000 || depth >= 3) return text;
        for (int i = 0; i < count && i < 12; ++i)
            text += (i ? "|" : ":") + Value(a->InnerProperty, data + size_t(i) * a->InnerProperty->ElementSize, depth + 1);
        return text;
    }
    return "<" + type + ">";
}

inline const FProperty* Property(const UStruct* type, const std::string& name) {
    for (const UStruct* s = type; s; s = s->Super)
        for (const FField* f = s->ChildProperties; f; f = f->Next)
            if (f->Name.ToString() == name) return static_cast<const FProperty*>(f);
    return nullptr;
}

inline bool Assign(const FProperty* p, std::uint8_t* at, const std::string& value) {
    const auto type = p->ClassPrivate->Name.ToString();
    if (type == "BoolProperty") {
        const auto* b = static_cast<const FBoolProperty*>(p);
        if (value != "0") at[b->ByteOffset] |= b->FieldMask; else at[b->ByteOffset] &= ~b->FieldMask;
    } else if (type == "IntProperty" || type == "UInt32Property") *reinterpret_cast<std::int32_t*>(at) = std::atoi(value.c_str());
    else if (type == "FloatProperty") *reinterpret_cast<float*>(at) = float(std::atof(value.c_str()));
    else if (type == "ByteProperty" || type == "Int8Property" || (type == "EnumProperty" && p->ElementSize == 1)) *at = std::uint8_t(std::atoi(value.c_str()));
    else if (type == "NameProperty")
        *reinterpret_cast<FName*>(at) = UKismetStringLibrary::Conv_StringToName(FString(state.strings.emplace_back(value.begin(), value.end()).c_str()));
    else if (type == "StrProperty")
        *reinterpret_cast<FString*>(at) = FString(state.strings.emplace_back(value.begin(), value.end()).c_str());
    else if (type == "ObjectProperty" || type == "ClassProperty") *reinterpret_cast<UObject**>(at) = value == "0" ? nullptr : Find(value);
    else if (type == "StructProperty") {
        // 结构体用逗号分隔的数字按成员顺序填写，例如向量 1,2,3。
        std::stringstream parts(value);
        std::string part;
        for (const FField* f = static_cast<const FStructProperty*>(p)->Struct->ChildProperties; f && std::getline(parts, part, ','); f = f->Next) {
            const auto* member = static_cast<const FProperty*>(f);
            if (!Assign(member, at + member->Offset, part)) return false;
        }
    } else return false;
    return true;
}

inline UFunction* Function(const UObject* object, const std::string& name) {
    for (const UStruct* s = object->Class; s; s = s->Super)
        for (UField* f = s->Children; f; f = f->Next)
            if (f->IsA(EClassCastFlags::Function) && f->GetName() == name) return static_cast<UFunction*>(f);
    return nullptr;
}

inline HWND Window() {
    struct Search { DWORD pid; HWND found; } search{GetCurrentProcessId(), nullptr};
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        auto* s = reinterpret_cast<Search*>(param);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        char type[64]{};
        GetClassNameA(window, type, sizeof(type));
        if (pid != s->pid || std::string(type) != "UnrealWindow" || GetWindow(window, GW_OWNER)) return TRUE;
        s->found = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
inline void Press(unsigned vk, unsigned hold, bool mouse) {
    std::thread([vk, hold, mouse] {
        HWND window = Window();
        if (!window) return;
        if (mouse) {
            const LPARAM at = MAKELPARAM(640, 360);
            PostMessageA(window, vk == 2 ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, vk == 2 ? MK_RBUTTON : MK_LBUTTON, at);
            Sleep(hold);
            PostMessageA(window, vk == 2 ? WM_RBUTTONUP : WM_LBUTTONUP, 0, at);
            return;
        }
        const LPARAM scan = LPARAM(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)) << 16;
        PostMessageA(window, WM_KEYDOWN, vk, scan | 1);
        Sleep(hold);
        PostMessageA(window, WM_KEYUP, vk, scan | 1 | (LPARAM(3) << 30));
    }).detach();
}

inline void Run(const std::string& line) {
    std::stringstream words(line);
    std::string verb, a, b, c;
    words >> verb >> a >> b;
    Out("> %s", line.c_str());
    if (verb == "objs") {
        int rows = 0;
        for (int i = 0; i < UObject::GObjects->Num() && rows < 120; ++i) {
            auto* object = At(i);
            if (!object || object->IsDefaultObject() || !ClassHas(object, a)) continue;
            const auto full = object->GetFullName();
            if (full.find("Default__") != std::string::npos) continue;
            if (!b.empty() && !Has(full, b)) continue;
            ++rows;
            if (object->IsA(AActor::StaticClass())) {
                auto* actor = static_cast<AActor*>(object);
                const auto at = actor->K2_GetActorLocation();
                Out("#%d %s owner=%s at=%.0f,%.0f,%.0f", i, full.c_str(), Ref(actor->Owner).c_str(), at.X, at.Y, at.Z);
            } else Out("#%d %s", i, full.c_str());
        }
        Out("objs rows=%d", rows);
    } else if (verb == "props") {
        auto* object = Find(a);
        if (!object) return Out("no-object");
        Out("%s", object->GetFullName().c_str());
        std::stringstream rows(Fields(object->Class, reinterpret_cast<const std::uint8_t*>(object), 0, b));
        for (std::string row; std::getline(rows, row);) Out("  %s", row.c_str());
    } else if (verb == "set") {
        words >> c;
        auto* object = Find(a);
        const auto* p = object ? Property(object->Class, b) : nullptr;
        if (!p) return Out("no-property");
        Out("set=%d", int(Assign(p, reinterpret_cast<std::uint8_t*>(object) + p->Offset, c)));
    } else if (verb == "call") {
        auto* object = Find(a);
        auto* function = object ? Function(object, b) : nullptr;
        if (!function) return Out("no-function object=%s", Ref(object).c_str());
        std::vector<std::uint8_t> frame(size_t(function->Size) + 64, 0);
        for (const FField* f = function->ChildProperties; f; f = f->Next) {
            const auto* p = static_cast<const FProperty*>(f);
            if (!(p->PropertyFlags & 0x80) || (p->PropertyFlags & 0x400)) continue;
            if (!(words >> c)) break;
            if (!Assign(p, frame.data() + p->Offset, c)) return Out("bad-argument %s", f->Name.ToString().c_str());
        }
        object->ProcessEvent(function, frame.data());
        Out("called %s.%s -> %s", Ref(object).c_str(), b.c_str(), Fields(function, frame.data(), 1, "").c_str());
    } else if (verb == "exec") {
        auto* object = Find(a);
        auto* function = object ? Function(object, b) : nullptr;
        if (!function) return Out("no-function");
        Out("exec %s rva=%llX flags=%X", b.c_str(), static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(function->ExecFunction) - reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr))),
            function->FunctionFlags);
    } else if (verb == "funcs") {
        auto* object = Find(a);
        if (!object) return Out("no-object");
        std::string text;
        for (const UStruct* s = object->Class; s; s = s->Super)
            for (UField* f = s->Children; f; f = f->Next)
                if (f->IsA(EClassCastFlags::Function) && (b.empty() || Has(f->GetName(), b))) text += f->GetName() + " ";
        Out("%s", text.substr(0, 1900).c_str());
    } else if (verb == "key") {
        Press(unsigned(std::strtoul(a.c_str(), nullptr, 16)), b.empty() ? 80 : unsigned(std::atoi(b.c_str())), false);
    } else if (verb == "click") {
        Press(a == "R" ? 2 : 1, b.empty() ? 80 : unsigned(std::atoi(b.c_str())), true);
    } else if (verb == "tp") {
        auto* actor = static_cast<AActor*>(Find(a));
        auto* target = static_cast<AActor*>(Find(b));
        float lift = 0;
        words >> lift;
        if (!actor || !target || !actor->IsA(AActor::StaticClass()) || !target->IsA(AActor::StaticClass())) return Out("no-actor");
        auto to = target->K2_GetActorLocation();
        to.Z += lift;
        Out("tp=%d", int(actor->K2_TeleportTo(to, actor->K2_GetActorRotation())));
    } else if (verb == "trace") {
        if (a == "off") { state.traces.clear(); state.mutes.clear(); state.stacks.clear(); }
        else if (a == "stack") state.stacks.push_back(b);
        else if (a == "mute") state.mutes.push_back(b);
        else { state.traces.push_back(a); state.traceRows = 0; }
    } else if (verb == "watch") {
        auto* object = Find(a);
        if (!object && a[0] == '#') return Out("no-object");
        state.watches.push_back({a[0] == '#' ? std::string() : a, object ? object->Index : -1, object ? object->Name : FName{}, b, ""});
    } else if (verb == "unwatch") state.watches.clear();
    else Out("unknown-verb");
}

inline void Poll() {
    const auto now = std::chrono::steady_clock::now();
    static bool busy = false;
    if (busy || now < state.next) return;
    struct Guard { ~Guard() { busy = false; } } guard;
    busy = true;
    state.next = now + std::chrono::milliseconds(150);
    const std::string base = std::string("logs/BRT-rig-") + state.side;
    if (!state.out.is_open()) {
        CreateDirectoryA("logs", nullptr);
        state.out.open(base + ".out", std::ios::trunc);
        Out("rig-ready side=%s pid=%lu", state.side, GetCurrentProcessId());
    }
    for (auto& watch : state.watches) {
        auto* object = watch.token.empty() ? At(watch.index) : Find(watch.token);
        std::string now_ = "gone";
        if (object && (!watch.token.empty() || object->Name == watch.name)) {
            now_.clear();
            std::stringstream names(watch.props);
            std::string name;
            while (std::getline(names, name, ','))
                if (const auto* p = Property(object->Class, name))
                    now_ += name + "=" + Value(p, reinterpret_cast<const std::uint8_t*>(object) + p->Offset, 1) + " ";
        }
        if (now_ != watch.last) { watch.last = now_; Out("watch %s %s", Ref(object).c_str(), now_.c_str()); }
    }
    std::ifstream commands(base + ".cmd");
    std::string line;
    for (size_t row = 0; std::getline(commands, line); ++row) {
        if (row < state.done) continue;
        ++state.done;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) Run(line);
    }
}

// 两侧 ProcessEvent 钩子在原函数返回后调用。
inline void Event(bool server, UObject* object, const std::string& function) {
    if (!Enabled()) return;
    state.side = server ? "server" : "client";
    if (!state.traces.empty() && state.traceRows < 20000) {
        for (const auto& part : state.traces) {
            if (!Has(function, part) && !(object && Has(object->GetName(), part))) continue;
            if (std::any_of(state.mutes.begin(), state.mutes.end(), [&](const std::string& m) { return Has(function, m); })) break;
            ++state.traceRows;
            std::string stack;
            if (std::any_of(state.stacks.begin(), state.stacks.end(), [&](const std::string& m) { return Has(function, m); })) {
                void* frames[28]{};
                const auto count = RtlCaptureStackBackTrace(1, 28, frames, nullptr);
                const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
                char rva[24]{};
                for (unsigned i = 0; i < count; ++i) {
                    const auto at = reinterpret_cast<std::uintptr_t>(frames[i]);
                    if (at < image || at - image > 0x0648C000) continue;
                    std::snprintf(rva, sizeof(rva), " %llX", static_cast<unsigned long long>(at - image));
                    stack += rva;
                }
            }
            Out("event %s %s%s", Ref(object).c_str(), function.c_str(), stack.c_str());
            break;
        }
    }
    const bool tick = function.size() > 5 && function.compare(function.size() - 5, 5, ".Tick") == 0;
    if (!state.thread && (tick || function.find("ReceiveTick") != std::string::npos)) state.thread = GetCurrentThreadId();
    if (state.thread == GetCurrentThreadId()) Poll();
}
}
