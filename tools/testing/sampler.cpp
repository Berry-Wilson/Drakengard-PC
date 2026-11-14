// Out-of-process sampling profiler: sampler.exe <pid> <seconds> <intervalMs> <out.txt>
// Suspends every thread of the target, records RIP plus a short stack, then resolves with DbgHelp.
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#pragma comment(lib, "dbghelp.lib")

static const int kDepth = 14;

int main(int argc, char** argv)
{
    if (argc < 5) { printf("usage: sampler pid seconds intervalMs out\n"); return 1; }
    DWORD pid = atoi(argv[1]);
    double seconds = atof(argv[2]);
    int interval = atoi(argv[3]);
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { printf("OpenProcess failed %lu\n", GetLastError()); return 1; }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(proc, nullptr, TRUE)) { printf("SymInitialize failed\n"); return 1; }

    struct Sample { DWORD tid; DWORD64 pc[kDepth]; int n; };
    std::vector<Sample> samples;
    std::map<DWORD, HANDLE> threads;
    timeBeginPeriod(1);
    DWORD start = GetTickCount();
    while ((GetTickCount() - start) < seconds * 1000.0)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof(te)};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid && !threads.count(te.th32ThreadID))
                threads[te.th32ThreadID] = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
        CloseHandle(snap);
        for (auto& [tid, h] : threads)
        {
            if (!h || SuspendThread(h) == (DWORD)-1) continue;
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(h, &ctx))
            {
                Sample s{tid, {}, 0};
                STACKFRAME64 f{};
                f.AddrPC.Offset = ctx.Rip; f.AddrPC.Mode = AddrModeFlat;
                f.AddrStack.Offset = ctx.Rsp; f.AddrStack.Mode = AddrModeFlat;
                f.AddrFrame.Offset = ctx.Rbp; f.AddrFrame.Mode = AddrModeFlat;
                while (s.n < kDepth && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, h, &f, &ctx, nullptr,
                                                   SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                {
                    if (!f.AddrPC.Offset) break;
                    s.pc[s.n++] = f.AddrPC.Offset;
                }
                if (s.n) samples.push_back(s);
            }
            ResumeThread(h);
        }
        Sleep(interval);
    }
    SymRefreshModuleList(proc);

    std::map<DWORD64, std::string> cache;
    auto name = [&](DWORD64 a) -> std::string {
        auto it = cache.find(a); if (it != cache.end()) return it->second;
        char buf[sizeof(SYMBOL_INFO) + 512]{}; auto* si = (SYMBOL_INFO*)buf;
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 511;
        std::string r; DWORD64 disp = 0;
        IMAGEHLP_MODULE64 mi{sizeof(mi)};
        if (SymFromAddr(proc, a, &disp, si)) r = si->Name;
        else { char t[64]; sprintf_s(t, "0x%llx", a); r = t; }
        if (SymGetModuleInfo64(proc, a, &mi)) r = std::string(mi.ModuleName) + "!" + r;
        return cache[a] = r;
    };

    std::map<DWORD, size_t> perThread;
    for (auto& s : samples) perThread[s.tid]++;
    FILE* out = fopen(argv[4], "w");
    fprintf(out, "samples %zu\n", samples.size());
    for (auto& [tid, n] : perThread)
    {
        std::map<std::string, size_t> self, incl, callerOfTop;
        for (auto& s : samples) if (s.tid == tid)
        {
            self[name(s.pc[0])]++;
            std::vector<std::string> seen;
            for (int i = 0; i < s.n; ++i) { auto nm = name(s.pc[i]); if (std::find(seen.begin(), seen.end(), nm) == seen.end()) { seen.push_back(nm); incl[nm]++; } }
            std::string chain = name(s.pc[0]);
            for (int i = 1; i < std::min(s.n, 3); ++i) chain += " <- " + name(s.pc[i]);
            callerOfTop[chain]++;
        }
        auto dump = [&](const char* title, std::map<std::string, size_t>& m, int lim) {
            std::vector<std::pair<size_t, std::string>> v; for (auto& [k, c] : m) v.push_back({c, k});
            std::sort(v.rbegin(), v.rend());
            fprintf(out, "  -- %s\n", title);
            for (int i = 0; i < (int)v.size() && i < lim; ++i) fprintf(out, "  %6.2f%% %6zu  %s\n", 100.0 * v[i].first / n, v[i].first, v[i].second.c_str());
        };
        fprintf(out, "\n== thread %lu: %zu samples\n", tid, n);
        if (n < samples.size() / 50) continue;
        dump("self", self, 40);
        {
            // Hottest instruction addresses with source lines (needs the PDB's line info).
            std::map<DWORD64, size_t> addrs;
            for (auto& s : samples) if (s.tid == tid) addrs[s.pc[0]]++;
            std::map<std::string, size_t> lines;
            for (auto& [a, c] : addrs)
            {
                IMAGEHLP_LINE64 li{sizeof(li)}; DWORD d = 0;
                std::string k = SymGetLineFromAddr64(proc, a, &d, &li) ? std::string(li.FileName) + ":" + std::to_string(li.LineNumber) : name(a);
                lines[k] += c;
            }
            dump("self by source line", lines, 60);
        }
        dump("inclusive (top 14 frames)", incl, 40);
        dump("stacks (3 frames)", callerOfTop, 40);
    }
    fclose(out);
    printf("wrote %zu samples\n", samples.size());
    return 0;
}
