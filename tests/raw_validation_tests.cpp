// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/raw_validation.hpp"
#include "valeo/journal.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <unistd.h>

using namespace valeo;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void rejects(const std::function<void()>& work){bool stopped=false;try{work();}catch(const Error&){stopped=true;}require(stopped,"Invalid raw input was accepted");}
int main(){try{
    Raw14FrameValidator good(8);good.append({0,0,0,1});good.append({0x12,0x34,0x3f,0xff});good.finish();
    require(good.minimum()==0 && good.maximum()==16383 && !good.invalid_samples(),"BE16 bounds were decoded incorrectly");
    // A fully saturated but in-range exposure is not an error sentinel.
    Raw14FrameValidator saturated(4);saturated.append({0x3f,0xff,0x3f,0xff});saturated.finish();
    Raw14FrameValidator bad(8);bad.append({0xff,0xff,0,1});
    require(bad.received()==4 && bad.invalid_samples()==1,"Invalid data must be counted without interrupting the drain");
    bad.append({0,2,0,3});require(bad.received()==8,"Full invalid frame was not drained");rejects([&]{bad.finish();});
    Raw14FrameValidator incomplete(4);incomplete.append({0,1});rejects([&]{incomplete.finish();});
    rejects([&]{incomplete.append({0});});rejects([&]{incomplete.append({0,2,0,3});});
    rejects([]{Raw14FrameValidator v(3);});rejects([]{Raw14FrameValidator v(512ull*1024*1024+2);});
    char pattern[]="/tmp/valeo-raw-tests-XXXXXX";const auto directory=::mkdtemp(pattern);require(directory,"Temporary directory failed");
    const std::filesystem::path root(directory),path=root/"invalid.vraw";
    {Journal journal(path,4);Raw14FrameValidator validator(4);Bytes sentinel{255,255,255,255};journal.append(sentinel);validator.append(sentinel);
     rejects([&]{validator.finish();journal.commit();});}
    require(!std::filesystem::exists(path) && std::filesystem::file_size(path.string()+".partial")==4,"Invalid frame was published or its diagnostic bytes lost");
    std::filesystem::remove_all(root);
    std::cout<<"PASS raw: BE16/14-bit bounds, complete drain before rejection, saturation, truncation and invalid journal retention\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
