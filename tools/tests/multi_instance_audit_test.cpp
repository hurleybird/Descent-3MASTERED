// Windows-only standalone regression checks for process-scoped file ownership.
#include "multi_instance.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    if (!strcmp(argv[1], "cleanup")) {
        MultiInstanceCleanupStaleProcessDirectories(argv[2], "audit-"); return 0;
    }
    if (!strcmp(argv[1], "hold")) {
        MultiInstanceFileLock lock;
        if (!lock.TryAcquire(argv[2])) return 3;
        std::string ready=std::string(argv[2])+".ready";
        FILE* f=fopen(ready.c_str(),"wb"); if (!f) return 4; fclose(f);
        Sleep(INFINITE); return 0;
    }
    if (strcmp(argv[1], "locks")) return 2;
    std::filesystem::create_directories(argv[2]);
    std::string path=std::string(argv[2])+"\\pilot.plt.lock";
    MultiInstanceFileLock first, second;
    bool ok=first.TryAcquire(path.c_str()) && !second.TryAcquire(path.c_str());
    first.Release(); ok=second.TryAcquire(path.c_str()) && ok; second.Release();
    printf("%s exclusive ownership and release\n",ok?"PASS":"FAIL");
    char exe[MAX_PATH]; GetModuleFileNameA(nullptr,exe,MAX_PATH);
    std::string child_path=std::string(argv[2])+"\\crash.plt.lock";
    std::filesystem::remove(child_path+".ready");
    std::string command=std::string("\"")+exe+"\" hold \""+child_path+"\"";
    STARTUPINFOA startup={sizeof(startup)}; PROCESS_INFORMATION process={};
    if (!CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return 5;
    bool ready=false;
    for (int i=0;i<100 && !ready;++i) {
        ready=GetFileAttributesA((child_path+".ready").c_str())!=INVALID_FILE_ATTRIBUTES;
        if (!ready) Sleep(20);
    }
    bool owned=ready && !first.TryAcquire(child_path.c_str());
    TerminateProcess(process.hProcess,0); WaitForSingleObject(process.hProcess,5000);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    bool recovered=first.TryAcquire(child_path.c_str());
    printf("%s lock released after forced process termination\n",owned&&recovered?"PASS":"FAIL");
    return ok && owned && recovered ? 0 : 1;
}
