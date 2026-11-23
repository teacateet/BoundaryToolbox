#pragma once

#include "json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace brt::spawn {

using Json = nlohmann::json;
using Name = std::string;
inline constexpr char None[] = "None";

struct Member {
    Name room, connection, steam;
};
struct Selection {
    Name role, primary, secondary;
    bool operator==(const Selection& other) const {
        return role == other.role && primary == other.primary && secondary == other.secondary;
    }
};
struct Appearance {
    Name skin{None}, paint{None}, special{None}, ornament{None};
};
struct Part {
    Name id{None}, skin{None}, special{None}, paint{None}, archiveOrnament{None};
};
struct ReceiverAppearance {
    Name part, skin, paint;
};
struct Weapon {
    Name id, ornament{None};
    std::optional<Name> classId;
    Appearance appearance;
    std::array<Part, 10> parts;
    std::optional<ReceiverAppearance> receiver;
    bool Present() const { return id != None; }
};
struct CharacterAppearance {
    std::optional<Name> suit, painting, badge, head;
};
struct Loadout {
    Name role, left, right, mobility, melee;
    Weapon primary, secondary;
    std::optional<CharacterAppearance> character;
};
enum class Result { Applied, AlreadyApplied, Rejected, HostDeclined };

inline Name ReadName(const Json& object, const char* key) {
    Name value = object.at(key).get<Name>();
    if (value.empty() || value.size() > 256 ||
        std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::invalid_argument(key);
    return value;
}
inline Weapon ReadWeapon(const Json& object, const Name& selected) {
    Weapon weapon;
    weapon.id = ReadName(object, "WeaponID");
    if (weapon.id != selected) throw std::invalid_argument("unexpected weapon");
    const auto& slots = object.at("WeaponPartSlotTypeArray");
    const auto& parts = object.at("WeaponPartConfigs");
    if (!slots.is_array() || !parts.is_array() || slots.size() != parts.size() ||
        slots.size() != (weapon.Present() ? weapon.parts.size() : 0))
        throw std::invalid_argument("weapon parts");
    if (!weapon.Present()) {
        if (object.contains("ReceiverMainAppearance")) throw std::invalid_argument("receiver on empty weapon");
        return weapon;
    }
    weapon.ornament = ReadName(object, "OrnamentID");
    if (object.contains("WeaponClassID")) weapon.classId = ReadName(object, "WeaponClassID");
    const auto& body = object.at("ArchiveAppearance");
    weapon.appearance = {ReadName(body, "SkinItem"), ReadName(body, "PaintItem"),
                         ReadName(body, "SpecialSlot"), ReadName(body, "Ornament")};
    if (object.contains("ReceiverMainAppearance")) {
        const auto& row = object.at("ReceiverMainAppearance");
        ReceiverAppearance look{ReadName(row, "WeaponPartID"), ReadName(row, "WeaponPartSkinID"),
                                ReadName(row, "WeaponPartSkinPaintingID")};
        if (look.part == None || look.skin == None || look.paint == None ||
            weapon.appearance.skin == None || weapon.appearance.paint == None)
            throw std::invalid_argument("incomplete receiver appearance");
        weapon.receiver = std::move(look);
    }
    std::array<bool, 10> seen{};
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].is_number_integer()) throw std::invalid_argument("slot must be an integer");
        const auto number = slots[i].get<std::int64_t>();
        if (number < 1 || number > 10 || seen[number - 1]) throw std::invalid_argument("weapon slot");
        seen[number - 1] = true;
        const auto& item = parts[i];
        weapon.parts[number - 1] = {ReadName(item, "WeaponPartID"), ReadName(item, "WeaponPartSkinID"),
                                    ReadName(item, "WeaponPartSpecialSkinID"), ReadName(item, "WeaponPartSkinPaintingID"),
                                    ReadName(item, "ArchiveOrnament")};
    }
    return weapon;
}

// 每个已认证连接持有一个对象。注册、回包、离房都在服务器游戏线程串行处理。
class Consumer {
    Member member_;
    Selection selection_;
    std::uint64_t life_{}, pawn_{};
    bool closed_{}, applying_{};
    std::optional<Json> committed_;
public:
    explicit Consumer(Member authenticated) : member_(std::move(authenticated)) {}
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;

    bool Begin(std::uint64_t life, std::uint64_t pawnGeneration, Selection chosen) {
        if (closed_ || applying_ || !life || !pawnGeneration || chosen.role.empty() || chosen.role == None ||
            member_.room.empty() || member_.connection.empty() || member_.steam.empty()) return false;
        // selection 由房主已批准的当前选择提供，枪名为角色限定 ID 或显式 None。
        auto allowed = [&](const Name& gun) {
            const auto prefix = chosen.role + "_";
            return gun == None || (gun.compare(0, prefix.size(), prefix) == 0 && gun.size() > prefix.size());
        };
        if (!allowed(chosen.primary) || !allowed(chosen.secondary)) return false;
        if (life == life_) return pawnGeneration == pawn_ && chosen == selection_;
        if (life != life_ + 1) return false;
        selection_ = std::move(chosen);
        life_ = life; pawn_ = pawnGeneration; committed_.reset();
        return true;
    }
    void Leave() { closed_ = true; committed_.reset(); }
    bool Targets(std::uint64_t pawnGeneration) const {
        return !closed_ && life_ && pawn_ == pawnGeneration;
    }

    template<class Commit>
    Result Accept(std::uint64_t currentPawnGeneration, std::string_view bytes, Commit&& commit) {
        if (!Targets(currentPawnGeneration) || applying_ || bytes.size() > 131072)
            return Result::Rejected;
        Json document = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (!document.is_object()) return Result::Rejected;
        Loadout plan;
        try {
            if (document.at("version") != 1 || document.at("roomId") != member_.room ||
                document.at("connectionId") != member_.connection || document.at("authenticatedSteamId") != member_.steam ||
                !document.at("life").is_number_unsigned() || document.at("life").get<std::uint64_t>() != life_)
                return Result::Rejected;
            const auto& data = document.at("loadout");
            if (data.at("CharacterID") != selection_.role) return Result::Rejected;
            if (committed_) return *committed_ == data ? Result::AlreadyApplied : Result::Rejected;
            plan = {selection_.role, ReadName(data.at("LeftLauncherData"), "ID"), ReadName(data.at("RightLauncherData"), "ID"),
                    ReadName(data.at("MobilityModuleData"), "MobilityModuleID"), ReadName(data.at("MeleeWeaponData"), "ID"),
                    ReadWeapon(data.at("FirstWeaponPartData"), selection_.primary), ReadWeapon(data.at("SecondWeaponPartData"), selection_.secondary), {}};
            if (data.contains("CharacterData")) {
                const auto& fields = data.at("CharacterData");
                if (!fields.is_object() || fields.empty()) return Result::Rejected;
                CharacterAppearance look;
                for (auto it = fields.begin(); it != fields.end(); ++it) {
                    const auto& key = it.key();
                    auto* value = key == "SpacesuitID" ? &look.suit : key == "SpacesuitSkinPaintingID" ? &look.painting :
                                  key == "ArmBadgeID" ? &look.badge : key == "HeadAccessoryID" ? &look.head : nullptr;
                    if (!value) return Result::Rejected;
                    *value = ReadName(fields, key.c_str());
                }
                if ((look.suit && *look.suit == None) || (look.suit.has_value() != look.painting.has_value())) return Result::Rejected;
                plan.character = std::move(look);
            }
        } catch (const std::exception&) { return Result::Rejected; }
        // 预先保留回执。只有宿主成功提交整份新生装备才推进同命去重状态。
        Json receipt = std::move(document.at("loadout"));
        applying_ = true;
        struct Clear { bool& flag; ~Clear() { flag = false; } } onReturn{applying_};
        if (!commit(plan)) return Result::HostDeclined;
        if (!closed_) committed_.emplace(std::move(receipt));
        return Result::Applied;
    }
};

} // namespace brt::spawn
