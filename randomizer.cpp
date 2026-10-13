#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// ---- Offsets: verify against your CE table if the sanity check fails ----
constexpr uintptr_t OFF_PLAYERGAMEDATA = 0x08;   // GameDataMan -> PlayerGameData
constexpr uintptr_t OFF_STATS          = 0x3C;   // Vigor, Mind, End, Str, Dex, Int, Faith, Arcane (uint32 each)
constexpr uintptr_t OFF_LEVEL          = 0x68;
constexpr int       BASE_POINTS        = 79;     // sum of stats == level + 79
constexpr uint32_t  STAT_CAP           = 99;
constexpr uint32_t  MIN_PER_STAT       = 0;      // set to 1 if you don't want zero stats

// GameDataMan pointer: mov rax,[rip+disp32]
const char* GDM_PATTERN = "48 8B 05 ? ? ? ? 48 85 C0 74 05 48 8B 40 58 C3 C3";

static void Log(const char* fmt, ...) {
    FILE* f = fopen("er_randomizer.log", "a");
    if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

static uintptr_t ScanModule(const char* pattern) {
    HMODULE mod = GetModuleHandleA(nullptr);
    auto dos = (PIMAGE_DOS_HEADER)mod;
    auto nt  = (PIMAGE_NT_HEADERS)((uint8_t*)mod + dos->e_lfanew);
    size_t size = nt->OptionalHeader.SizeOfImage;
    std::vector<int> pat;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') { pat.push_back(-1); p += (p[1] == '?') ? 2 : 1; }
        else { pat.push_back((int)strtoul(std::string(p, 2).c_str(), nullptr, 16)); p += 2; }
    }
    auto base = (uint8_t*)mod;
    for (size_t i = 0; i + pat.size() < size; ++i) {
        bool ok = true;
        for (size_t j = 0; j < pat.size(); ++j)
            if (pat[j] != -1 && base[i + j] != pat[j]) { ok = false; break; }
        if (ok) return (uintptr_t)(base + i);
    }
    return 0;
}

static uintptr_t g_gdmPtrAddr = 0;

// Returns PlayerGameData* or nullptr. SEH-guarded.
static uint8_t* GetPlayerData() {
    __try {
        uintptr_t gdm = *(uintptr_t*)g_gdmPtrAddr;
        if (!gdm) return nullptr;
        return (uint8_t*)*(uintptr_t*)(gdm + OFF_PLAYERGAMEDATA);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool ReadState(uint8_t* pd, uint32_t& level, std::array<uint32_t, 8>& s) {
    __try {
        level = *(uint32_t*)(pd + OFF_LEVEL);
        for (int i = 0; i < 8; ++i) s[i] = *(uint32_t*)(pd + OFF_STATS + i * 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool Consistent(uint32_t level, const std::array<uint32_t, 8>& s) {
    uint32_t sum = 0;
    for (auto v : s) sum += v;
    return level >= 1 && level <= 713 && sum == level + BASE_POINTS;
}

static std::mt19937 rng{ std::random_device{}() };

static std::array<uint32_t, 8> Roll(uint32_t level) {
    std::array<uint32_t, 8> st{};
    int leftover = (int)level + BASE_POINTS;
    for (auto& v : st) { v = MIN_PER_STAT; leftover -= MIN_PER_STAT; }

    // same chunking as the original script, but respecting the cap
    while (leftover > 0) {
        int chunk = std::max(1, leftover / 8);
        int idx = std::uniform_int_distribution<int>(0, 7)(rng);
        int room = (int)STAT_CAP - (int)st[idx];
        if (room <= 0) continue;               // stat full, pick another
        int add = std::min(chunk, room);
        st[idx] += add;
        leftover -= add;
    }
    return st;
}

static void Randomize(uint8_t* pd, uint32_t level) {
    auto st = Roll(level);
    __try {
        for (int i = 0; i < 8; ++i) *(uint32_t*)(pd + OFF_STATS + i * 4) = st[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("write failed"); return; }
    Log("Level %u -> Vig %u Mind %u End %u Str %u Dex %u Int %u Fai %u Arc %u",
        level, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7]);
}

static DWORD WINAPI Worker(LPVOID) {
    Sleep(15000); // let the game initialise

    uintptr_t hit = 0;
    while (!(hit = ScanModule(GDM_PATTERN))) Sleep(1000);
    g_gdmPtrAddr = hit + 7 + *(int32_t*)(hit + 3);
    Log("GameDataMan ptr at %p", (void*)g_gdmPtrAddr);

    uint32_t prevLevel = 0;
    bool validated = false;

    for (;;) {
        Sleep(200);
        uint8_t* pd = GetPlayerData();
        if (!pd) { prevLevel = 0; continue; }   // main menu / loading

        uint32_t level; std::array<uint32_t, 8> s;
        if (!ReadState(pd, level, s)) { prevLevel = 0; continue; }

        bool forced = (GetAsyncKeyState(VK_F9) & 1);

        if (!Consistent(level, s)) {
            // mid-transition or wrong offsets
            if (!validated) Log("sanity check failed (level %u) - check offsets", level);
            if (!forced) continue;
            continue;
        }
        if (!validated) { validated = true; Log("offsets validated"); }

        bool levelUp = prevLevel != 0 && level > prevLevel;
        if (levelUp || forced) {
            Sleep(300); // let the game finish applying the level-up
            if (ReadState(pd, level, s) && Consistent(level, s))
                Randomize(pd, level);
        }
        prevLevel = level;
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}