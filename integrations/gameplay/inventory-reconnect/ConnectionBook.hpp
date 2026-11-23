#pragma once
#include <optional>
#include <string>
#include <utility>

namespace brt::reconnect {
template<class Identity>
class ConnectionBook {
public:
    struct Entry {
        Identity controller, transport;
        std::string request, previous;
        bool menuHandled = false;
        bool verified = false;
        bool birthReady = false;
        bool Finished() const { return menuHandled && birthReady; }
    };
private:
    std::string room_, owner_;
    std::optional<Entry> current_;
    std::string confirmed_;
public:
    // 同房同人只有旧控制器或旧传输确实失效才轮换。相同标识允许未成功时重试。
    template<class Disconnected, class NewRequest>
    Entry* Start(const std::string& room, const std::string& owner, Identity controller,
                 Identity transport, Disconnected&& lost, NewRequest&& make) {
        if (room_ == room && owner_ != owner && !confirmed_.empty()) return nullptr;
        if (room_ != room || owner_ != owner) {
            current_.reset();
            confirmed_.clear();
            room_ = room;
            owner_ = owner;
        }
        std::string previous;
        if (current_) {
            if (current_->controller == controller && current_->transport == transport)
                return current_->Finished() ? nullptr : &*current_;
            if (!lost(current_->controller,current_->transport)) return nullptr;
            previous = current_->request;
        }
        std::string request = make();
        if (request.empty()) return nullptr;
        current_.emplace(Entry{controller,transport,std::move(request),std::move(previous)});
        return &*current_;
    }
    const std::string& Confirmed() const { return confirmed_; }
    bool IsDifferentVerifiedSession(const std::string& room,const std::string& owner,Identity controller,Identity transport) const {
        return current_ && current_->verified && room_==room && owner_==owner &&
               (!(current_->controller==controller) || !(current_->transport==transport));
    }
    template<class Disconnected>
    bool PreviousIsDetached(Disconnected&& lost) const {
        return current_ && lost(current_->controller,current_->transport);
    }
    void Verified(const std::string& id, const std::string& predecessor) {
        if (!current_ || current_->request != id) return;
        confirmed_ = id;
        current_->previous = predecessor;
        current_->verified = true;
    }
    Entry* VerifiedEntry(const std::string& id) {
        return current_ && current_->verified && current_->request == id ? &*current_ : nullptr;
    }
};
}
