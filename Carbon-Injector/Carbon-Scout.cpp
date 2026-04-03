#include <Windows.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include "Scanner.hpp"

// Project Carbon 2.0 | Diagnostic Scout | "Archeology Phase"
// Goal: Structural Verification of the new Byfron Engine Layout

uintptr_t GetModuleBase(HANDLE hProc, const char* szMod)
{
    HMODULE hMods[1024];
    DWORD cbNeeded;
    if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded))
    {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            char szModName[MAX_PATH];
            if (GetModuleFileNameExA(hProc, hMods[i], szModName, sizeof(szModName)))
            {
                if (strstr(szModName, szMod) != NULL) return (uintptr_t)hMods[i];
            }
        }
    }
    return 0;
}

void HexDump(const BYTE* data, size_t size, uintptr_t base = 0) {
    for (size_t i = 0; i < size; i += 16) {
        std::cout << " [0x" << std::hex << std::setw(8) << std::setfill('0') << (base + i) << "] ";
        for (int j = 0; j < 16; j++) {
            if (i + j < size) std::cout << std::setw(2) << (int)data[i+j] << " ";
            else std::cout << "   ";
        }
        std::cout << " | ";
        for (int j = 0; j < 16; j++) {
            if (i + j < size) {
                char c = (char)data[i+j];
                std::cout << (c >= 32 && c <= 126 ? c : '.');
            }
        }
        std::cout << std::endl;
    }
}

int main() {
    SetConsoleTitleA("Carbon 2.0 | Diagnostic Scout | Structural Archeology");
    std::cout << "\n [!] Carbon Scout: Initializing Structural Forensics...";

    HWND hWindow = NULL;
    while (hWindow == NULL || !IsWindowVisible(hWindow)) {
        hWindow = FindWindowA(NULL, "Roblox");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    DWORD PID = 0;
    GetWindowThreadProcessId(hWindow, &PID);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
    if (!hProc) { std::cout << "\n [-] Error: Failed to open process."; return 0; }

    std::cout << "\n [+] Target Identified: PID " << std::dec << PID;
    uintptr_t robloxBase = GetModuleBase(hProc, "RobloxPlayerBeta.exe");
    auto text = Scanner::GetSectionFractal(hProc, robloxBase, ".text");
    
    // Phase 1: High-Level Anchor (TaskScheduler)
    int jobVectorOffset = 0;
    uintptr_t taskScheduler = Scanner::BlindHeuristicScrawl(hProc, text, jobVectorOffset);
    if (!taskScheduler) { std::cout << "\n [-] Error: TaskScheduler anchor failed."; return 0; }

    std::cout << "\n [+] ANCHORED: TaskScheduler at [0x" << std::hex << taskScheduler << "]";
    std::cout << "\n [!] Archeology: Scrawling structural header (0x500 bytes)...";
    
    BYTE tsHeader[0x500];
    ReadProcessMemory(hProc, (LPCVOID)taskScheduler, tsHeader, 0x500, nullptr);
    HexDump(tsHeader, 0x500, taskScheduler);

    // Phase 2: Structural Discovery (Search for JobVector candidates)
    std::cout << "\n [!] Identity: Searching for JobVector candidates (begin, end, end_cap)...";
    std::vector<int> candidates;
    for (int off = 0x8; off <= 0x500 - 24; off += 8) {
        uintptr_t* ptrs = reinterpret_cast<uintptr_t*>(&tsHeader[off]);
        if (ptrs[0] != 0 && ptrs[1] != 0 && ptrs[2] != 0) {
            if (ptrs[0] < ptrs[1] && ptrs[1] <= ptrs[2]) {
                if ((ptrs[1] - ptrs[0]) % 8 == 0 && (ptrs[1] - ptrs[0]) < 0x2000) {
                    std::cout << "\n [+] CANDIDATE: Found vector-like at Offset [0x" << std::hex << off << "]";
                    candidates.push_back(off);
                }
            }
        }
    }

    if (candidates.empty()) { std::cout << "\n [-] Error: No structural candidates found. Structure is fully obfuscated."; return 0; }

    // Phase 3: Job Archeology
    int choice = candidates[0]; // Usually the first one
    std::cout << "\n [!] Identity: Auditing 20 jobs from Candidate [0x" << std::hex << choice << "]...";
    uintptr_t jobVector[3];
    memcpy(jobVector, &tsHeader[choice], sizeof(jobVector));

    std::cout << "\n [!] Vector: 0x" << std::hex << jobVector[0] << " -> 0x" << jobVector[1];
    
    uintptr_t current = jobVector[0];
    int count = 0;
    while (current < jobVector[1] && count < 20) {
        uintptr_t jobPtr;
        ReadProcessMemory(hProc, (LPCVOID)current, &jobPtr, sizeof(jobPtr), nullptr);
        if (jobPtr) {
            std::cout << "\n\n [!] JOB [" << std::dec << count << "] at [0x" << std::hex << jobPtr << "]";
            BYTE jobData[0x100];
            ReadProcessMemory(hProc, (LPCVOID)jobPtr, jobData, 0x100, nullptr);
            HexDump(jobData, 0x100, jobPtr);

            // Search for strings (Forensic Audit)
            for (int i = 0; i < 0x100 - 9; i++) {
                if (tolower(jobData[i]) == 'h' && tolower(jobData[i+1]) == 'e' && tolower(jobData[i+2]) == 'a') {
                    std::cout << "\n [+] STRING DETECTED: [";
                    for (int j = 0; j < 16 && (i+j) < 0x100; j++) std::cout << (char)jobData[i+j];
                    std::cout << "] at job offset [0x" << std::hex << i << "]";
                }
            }
        }
        current += 8;
        count++;
    }

    std::cout << "\n\n [!] Archeology Complete. Analysis Required.";
    std::cout << "\n [!] Press ENTER to close...";
    CloseHandle(hProc);
    std::cin.get();
    return 0;
}
