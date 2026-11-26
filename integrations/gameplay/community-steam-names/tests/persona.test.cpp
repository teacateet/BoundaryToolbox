#include "../Persona.hpp"
#include <cstdio>
#include <string>
using namespace brt::persona;
struct Friends {
    std::string own,peer;
    bool pending{};
    unsigned selfReads{},requests{},peerReads{};
    Id requested{};
    const char* Self(){++selfReads;return own.c_str();}
    bool Pending(Id id){++requests;requested=id;return pending;}
    const char* Peer(Id id){++peerReads;return id==requested ? peer.c_str() : nullptr;}
};
int failures{},checks{};
void test(const char* label,bool ok){++checks;if(!ok)++failures; std::printf("%s %s\n",ok?"PASS":"FAIL",label);}
int main() {
    // 仅合成账户位布局；所有 Steam 方法都是内存替身，不访问个人资料。
    constexpr Id base=0x0110000100000000ULL, me=base|420001, peer=base|420002;
    struct Options{bool CustomPlayerName;};Options normal{false},streamer{true};
    test("opted-in streamer mode and unknown settings keep native anonymous names",
         DisplayEnabled(&normal)&&!DisplayEnabled(&streamer)&&!DisplayEnabled<Options>(nullptr));
    test("canonical individual account",Account(std::to_string(me))==me);
    const auto canonical=std::to_wstring(me);auto alias=canonical;alias[3]+=0x100;
    test("wide account must contain only literal ASCII digits",Account(canonical)==me && !Account(alias));
    test("empty account and nondecimal rejected",!Account("") && !Account("0000000000000001x"));
    test("clan and zero accounts rejected",!Individual((me & ~(Id(15)<<52)) | (Id(7)<<52)) && !Individual(base));
    test("alternate fillers cannot create account",Account(" "+std::to_string(me))==0);
    Friends f; f.own=reinterpret_cast<const char*>(u8"边境 Pilot🛰️"); f.peer=reinterpret_cast<const char*>(u8"同伴-MiXeD");
    auto local=Read(f,me,me,me,true);test("roundtrip nonascii supplementary and case",local&&*local==L"边境 Pilot🛰️");
    test("self uses only owning persona",f.selfReads==1 && f.requests==0 && f.peerReads==0);
    test("self never used for different player",!Read(f,me,me,peer,true) && f.selfReads==1);
    test("guest session identity mismatch leaves native name",!Read(f,me,peer,me,true) && !Read(f,me,peer,peer,false) && f.requests==0);
    test("remote row cannot alias local account",!Read(f,me,me,me,false) && f.requests==0);
    f.pending=true;test("pending Steam information never supplies placeholder",!Read(f,me,me,peer,false) && f.requests==1 && f.peerReads==0);
    f.pending=false;auto other=Read(f,me,me,peer,false);
    test("only matching remote Steam account and display name",other&&*other==L"同伴-MiXeD"&&f.requested==peer&&f.peerReads==1);
    f.peer="[unknown]";test("Steam missing-peer sentinel keeps native text",!Read(f,me,me,peer,false));
    f.own="";test("unavailable local persona keeps native text",!Read(f,me,me,me,true));
    test("invalid UTF-8 not widened byte by byte",!Decode("\xe6\xb8") && !Decode("\xc0\xaf") && !Decode("\xed\xa0\x80"));
    test("multiline and empty presentation rejected",!Decode("x\nother") && !Decode("x\t") && !Decode("   "));
    auto longName=std::string(120,'A');test("not limited to old UE name buffer",Decode(longName.c_str())==std::optional<std::wstring>(std::wstring(120,L'A')));
    test("bounded decoding never truncates into a wrong name",!Decode(std::string(513,'A').c_str()));
    Publication pub;int one{},two{}; const auto t=Clock::now();std::wstring native=L"Player",wanted=L"MiXeD";
    test("own native update only once while replication pending",pub.ShouldSubmit(&one,me,wanted,native,t)&&!pub.ShouldSubmit(&one,me,wanted,native,t));
    test("acknowledged name is not resubmitted",!pub.ShouldSubmit(&one,me,wanted,wanted,t+std::chrono::seconds(50)));
    test("limited retries when original server declines display updates",
        pub.ShouldSubmit(&one,me,wanted,native,t+std::chrono::seconds(60))&&pub.ShouldSubmit(&one,me,wanted,native,t+std::chrono::seconds(66))&&
        !pub.ShouldSubmit(&one,me,wanted,native,t+std::chrono::seconds(100)));
    test("new player state or new official name gets its own update",pub.ShouldSubmit(&two,me,wanted,native,t)&&pub.ShouldSubmit(&two,me,L"新名",wanted,t));
    std::printf("SUMMARY checks=%d failed=%d\n",checks,failures);return failures?1:0;
}
