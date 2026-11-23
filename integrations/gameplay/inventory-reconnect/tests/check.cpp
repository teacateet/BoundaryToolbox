#include "ConnectionBook.hpp"
#include "RoomHttp.hpp"
#include <cassert>
#include <iostream>

int main(int argc,char** argv) {
    using namespace brt;
    if(argc==1) {
        reconnect::ConnectionBook<int> connections;
        int sequence=0;
        bool lost=false;
        auto next=[&]{return std::to_string(++sequence);};
        auto old=[&](int,int){return lost;};
        auto start=[&](const char* who,int pc,int net){return connections.Start("room",who,pc,net,old,next);};
        auto* stranger=start("wrong",8,9);assert(stranger);
        auto* first=start("owner",1,2);assert(first && first->request=="2" && first->previous.empty());
        connections.Verified("2","");
        assert(start("wrong",8,9)==nullptr);
        assert(start("owner",1,2)->request=="2");
        connections.VerifiedEntry("2")->menuHandled = true;
        assert(start("owner",1,2));
        connections.VerifiedEntry("2")->birthReady = true;
        assert(start("owner",1,2)==nullptr && start("owner",3,4)==nullptr);
        lost=true;
        auto* second=start("owner",3,4);assert(second && second->previous=="2" && second->request=="3");
        connections.Verified("3","2");
        connections.VerifiedEntry("3")->birthReady=true;
        connections.VerifiedEntry("3")->menuHandled=true;
        assert(!start("owner",3,4));
        auto* third=start("owner",5,6);assert(third && third->previous=="3");
        std::cout<<"PASS pending recovery, current owner, replay, active old transport, different live PC and genuine disconnect\n";
        return 0;
    }
    if(argc!=6)return 2;
    try {
        const auto room=spawn::RoomHttp::FromLocalServer(static_cast<unsigned short>(std::stoi(argv[1])));
        if(!room)return 3;
        const std::string operation=argv[2];
        const spawn::Member member{room->RoomId(),argv[3],argv[4]};
        if(operation=="read")std::cout<<room->ReconnectMenu(member,argv[5]);
        else if(operation=="old")std::cout<<room->LoginMenu(member);
        else if(operation=="ack")room->MenuSubmitted(member);
        else return 4;
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what();return 5;}
}
