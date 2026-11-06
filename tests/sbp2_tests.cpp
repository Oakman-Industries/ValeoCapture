// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/sbp2.hpp"
#include <iostream>
#include <stdexcept>
using namespace valeo;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    Bytes block{1,0,0,0,0,0,0,0};
    Sbp2Completion c(2,5);c.notify(1,5,6,block.data(),block.size());require(c.state()==CompletionState::pending,"Late callback matched another ORB");
    c.notify(2,5,6,block.data(),block.size());block[1]=0xff;require(c.state()==CompletionState::success && c.status()[1]==0,"Callback data not copied");
    c.notify(2,5,7,nullptr,0);require(c.state()==CompletionState::success,"Duplicate overwrote result");block[1]=0;
    Sbp2Completion reset(1,5);reset.notify(1,6,6,block.data(),block.size());require(reset.state()==CompletionState::reset,"Old generation accepted");
    Sbp2Completion timeout(1,5);timeout.notify(1,5,7,nullptr,0);require(timeout.state()==CompletionState::timeout,"Timeout missed");
    for(std::size_t length=0;length<8;++length){Sbp2Completion x(1,5);x.notify(1,5,6,block.data(),length);require(x.state()==CompletionState::rejected,"Truncated status accepted");}
    for(auto mask:{0x08,0x10,0x20}){auto b=block;b[0]|=mask;Sbp2Completion x(1,5);x.notify(1,5,6,b.data(),b.size());require(x.state()==CompletionState::rejected,"Dead/error status accepted");}
    auto extra=block;extra[0]=2;extra.resize(12);extra[8]=2;Sbp2Completion x(1,5);x.notify(1,5,6,extra.data(),extra.size());require(x.state()==CompletionState::rejected,"Unknown target status accepted");
    std::cout<<"PASS SBP2 completion: stale tokens/generations, borrowed callback copy, timeout, malformed and dead target status\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
