#pragma once
#include "SpawnConsumer.hpp"

namespace brt::spawn {

// 此对象归专服工作线程所有。I/O 完成后将字符串和原出生编号投递回同一游戏线程。
class RoomHttp {
    unsigned short port_;
    Name room_, credential_;
    std::string Post(const wchar_t* operation, const Json& body, unsigned long status, bool atLogin = false) const;
public:
    RoomHttp(unsigned short port, Name room, Name credential);
    static std::optional<RoomHttp> FromLocalServer(unsigned short port = 8000);
    const Name& RoomId() const { return room_; }
    std::string LoginMenu(const Member& authenticated) const;
    void Admit(const Member& authenticated) const;
    std::string InitialMenu(const Name& connection) const;
    std::string Prepare(const Name& connection, std::uint64_t life, const Selection& chosen) const;
    void Leave(const Name& connection) const;
};

} // namespace brt::spawn
