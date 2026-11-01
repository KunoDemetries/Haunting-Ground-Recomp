#pragma once
// "Play modded": start the unmodified game on the exported disc image (separate process).
// Paths follow the existing launchers: Play.cmd in a beta folder (data\toc.bin,
// saves\memory-card-slot1.ps2 beside the game) or RUN_MANUAL_AUTHENTIC_TOC.cmd in the
// development tree (%TEMP%\haunting-toc-probe\synthetic-index-pattern.bin, the card in
// Documents\Haunting Ground Saves).
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace hgw {

struct GameLaunch {
    std::filesystem::path exe,working,toc,card;
};

inline std::filesystem::path env_path(const char* name) {
    const char* v=std::getenv(name);return v?std::filesystem::path(v):std::filesystem::path();
}

// `disc` is the source image/DATA.CVM; `editor` the editor executable; `game` an explicit
// override (may be empty).
inline GameLaunch find_game(const std::filesystem::path& disc,const std::filesystem::path& editor,const std::filesystem::path& game) {
    GameLaunch l;
    const auto root=std::filesystem::absolute(disc).parent_path().parent_path();   // folder holding "Haunting Ground (USA)"
    l.working=root;
    std::vector<std::filesystem::path> candidates;
    if(!game.empty())candidates.push_back(game);
    const auto here=std::filesystem::absolute(editor).parent_path();
    for(const auto& c:{here/"hg_game.exe",root/"hg_game.exe",root/"build"/"Release"/"hg_game.exe",here.parent_path().parent_path()/"Release"/"hg_game.exe",
                       here/"hg_game",root/"build"/"linux"/"hg_game"})candidates.push_back(c);
    for(const auto& c:candidates)if(std::filesystem::exists(c)){l.exe=c;break;}
    if(l.exe.empty())throw std::runtime_error("cannot find hg_game (pass --game PATH)");
    if(std::filesystem::exists(root/"data"/"toc.bin"))l.toc=root/"data"/"toc.bin";
    else l.toc=env_path("TEMP")/"haunting-toc-probe"/"synthetic-index-pattern.bin";
    if(!std::filesystem::exists(l.toc))throw std::runtime_error("cannot find the TOC record ("+l.toc.string()+")");
    if(std::filesystem::exists(root/"saves"))l.card=root/"saves"/"memory-card-slot1.ps2";
    else {
        const auto docs=env_path("USERPROFILE")/"Documents"/"Haunting Ground Saves";
        std::filesystem::create_directories(docs);
        l.card=docs/"memory-card-slot1.ps2";
    }
    return l;
}

inline void start_game(const GameLaunch& l,const std::filesystem::path& disc_image) {
    const std::vector<std::string> args={l.exe.string(),"--disc",disc_image.string(),"--toc-record",l.toc.string(),
        "--memory-card",l.card.string(),"--slices","1000000000000","--gpu-resident","--gpu-triangles","--spu2-output-core","0"};
#ifdef _WIN32
    std::wstring command;
    for(const auto& a:args) {
        if(!command.empty())command+=L' ';
        command+=L'"'+std::filesystem::path(a).wstring()+L'"';
    }
    STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,DETACHED_PROCESS,nullptr,l.working.wstring().c_str(),&si,&pi))
        throw std::runtime_error("could not start the game (error "+std::to_string(GetLastError())+")");
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
#else
    std::vector<char*> argv;for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    const auto previous=std::filesystem::current_path();std::filesystem::current_path(l.working);
    pid_t pid;const int rc=posix_spawn(&pid,argv[0],nullptr,nullptr,argv.data(),environ);
    std::filesystem::current_path(previous);
    if(rc)throw std::runtime_error("could not start the game");
#endif
}
}
