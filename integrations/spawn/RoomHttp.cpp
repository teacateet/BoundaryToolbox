#include "RoomHttp.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <memory>
#include <chrono>
#include <stdexcept>
#pragma comment(lib, "winhttp.lib")

namespace brt::spawn {
namespace {
struct CloseHandle {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandle>;
void Check(bool ok) {
    if (!ok) throw std::runtime_error("room HTTP transport failed");
}
}

RoomHttp::RoomHttp(unsigned short port, Name room, Name credential)
    : port_(port), room_(std::move(room)), credential_(std::move(credential)) {
    const auto roomChar = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-'; };
    const auto hex = [](char c) { return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'); };
    if (!port_ || room_.empty() || room_.size() > 80 || !std::all_of(room_.begin(), room_.end(),roomChar) ||
        credential_.size() != 64 || !std::all_of(credential_.begin(),credential_.end(),hex))
        throw std::invalid_argument("invalid room HTTP configuration");
}

// 只读取工具箱交给本次专服进程的环境。大厅不会获得这两项，也不能用自己的 Gate 票据代替。
std::optional<RoomHttp> RoomHttp::FromLocalServer(unsigned short port) {
    char room[81]{}, key[65]{};
    const DWORD n = GetEnvironmentVariableA("BRT_LOCAL_SPAWN_ROOM", room, sizeof(room));
    const DWORD k = GetEnvironmentVariableA("BRT_LOCAL_SPAWN_TOKEN", key, sizeof(key));
    if (!n && !k) return std::nullopt;
    if (n != 40 || k != 64 || std::string_view(room, 4) != "pve-")
        throw std::invalid_argument("invalid local server registration");
    return RoomHttp(port, Name(room, n), Name(key, k));
}

std::string RoomHttp::Post(const wchar_t* operation, const Json& body, DWORD expected, bool atLogin) const {
    const Handle session(WinHttpOpen(L"BRT room inventory/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
    Check(bool(session));
    Check(atLogin ? WinHttpSetTimeouts(session.get(), 100, 120, 120, 250)
                  : WinHttpSetTimeouts(session.get(), 2000, 2000, 4000, 4000));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
    const Handle local(WinHttpConnect(session.get(), L"127.0.0.1", port_, 0));
    Check(bool(local));
    const std::wstring path = L"/api/brt/v1/rooms/" + std::wstring(room_.begin(),room_.end()) + L"/spawn/" + operation;
    const Handle request(WinHttpOpenRequest(local.get(),L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
    Check(bool(request));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    Check(WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    const std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " +
                                  std::wstring(credential_.begin(),credential_.end()) + L"\r\n";
    auto bytes = body.dump();
    const DWORD length = static_cast<DWORD>(bytes.size());
    Check(WinHttpSendRequest(request.get(),headers.c_str(),static_cast<DWORD>(headers.size()),bytes.data(),length,length,0));
    Check(WinHttpReceiveResponse(request.get(), nullptr));
    DWORD status = 0, size = sizeof(status);
    Check(WinHttpQueryHeaders(request.get(),WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if (status != expected) throw std::runtime_error("room request rejected with HTTP " + std::to_string(status));
    std::string reply;
    char data[4096]; DWORD read = 0;
    do {
        if (atLogin) Check(std::chrono::steady_clock::now() < deadline);
        Check(WinHttpReadData(request.get(), data, sizeof(data), &read));
        if (reply.size() + read > (atLogin ? 16384u : 131072u)) throw std::runtime_error("oversize room response");
        reply.append(data,read);
    } while (read != 0);
    return reply;
}
std::string RoomHttp::LoginMenu(const Member& authenticated) const {
    if (authenticated.room != room_) throw std::invalid_argument("room mismatch");
    return Post(L"login-menu",{{"connectionId",authenticated.connection},{"authenticatedSteamId",authenticated.steam}},200,true);
}
void RoomHttp::Admit(const Member& authenticated) const {
    if (authenticated.room != room_) throw std::invalid_argument("room mismatch");
    Post(L"admit",{{"connectionId",authenticated.connection},{"authenticatedSteamId",authenticated.steam}},204);
}
std::string RoomHttp::InitialMenu(const Name& connection) const {
    return Post(L"menu", {{"connectionId",connection}}, 200);
}
std::string RoomHttp::Prepare(const Name& connection, std::uint64_t life, const Selection& chosen) const {
    return Post(L"prepare",{{"connectionId",connection},{"life",life},{"selection",{
        {"role",chosen.role},{"primary",chosen.primary},{"secondary",chosen.secondary}}}},200);
}
void RoomHttp::Leave(const Name& connection) const {
    Post(L"leave",{{"connectionId",connection}},204);
}
} // namespace brt::spawn
