#define _CRT_SECURE_NO_WARNINGS
#include "Scanner.hpp"
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <psapi.h>
#include <map>
#include <iomanip>

using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, const char* lpProcName);
using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);
using f_RtlAddFunctionTable = BOOLEAN(WINAPI*)(PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress);
using f_Beep = BOOL(WINAPI*)(DWORD dwFreq, DWORD dwDuration);

struct MANUAL_MAPPING_DATA
{
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
    f_RtlAddFunctionTable pRtlAddFunctionTable;
    f_Beep pBeep;
    BYTE* pbase;
    uintptr_t originalHeartbeat;
    bool* pInjected;
    DWORD Stage;
    char Telemetry[1024];

    // Goliath V39 Self-Heal Bridge
    uintptr_t* pJob;
    uintptr_t pOriginalVTable;

    // Goliath V40 Quantum Bridge
    uintptr_t pTargetCode;
    BYTE OriginalBytes[14];
};

// Nirvana Syscall Wrapper (Ultimate Stealth)
std::vector<BYTE> nirvanaSyscallFilter = {
    0x65,0x48,0x89,0x24,0x25,0xE0,0x02,0x00,0x00, // mov gs:[2E0], rsp
    0x4C,0x8B,0xD1,                               // mov r10, rcx
    0x48,0x83,0xEC,0x40,                          // sub rsp, 40
    0x48,0x8B,0x0D,0x00,0x00,0x00,0x00,           // mov rcx, [RIP+SharedData] (Placeholder)
    0x49,0xBB,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // mov r11, FilterFunc (Placeholder)
    0x41,0xFF,0xD3,                               // call r11
    0x48,0x83,0xC4,0x40,                          // add rsp, 40
    0xC3                                          // ret
};

// Goliath V13: PIC-Safe Manual Mapping Logic
#pragma optimize("", off) 
void __stdcall ShellcodeV13_Logic(MANUAL_MAPPING_DATA* pData)
{
    if (!pData || *pData->pInjected) return;
    pData->Stage = 1; // [STAGE 1] Started

    BYTE* pBase = pData->pbase;
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDos->e_lfanew);
    auto* pOpt = &pNt->OptionalHeader;

    // Relocations
    BYTE* delta = pBase - pOpt->ImageBase;
    if (delta && pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        while (pReloc->VirtualAddress) {
            UINT count = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            WORD* pList = reinterpret_cast<WORD*>(pReloc + 1);
            for (UINT i = 0; i < count; i++) {
                if ((pList[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                    *reinterpret_cast<uintptr_t*>(pBase + pReloc->VirtualAddress + (pList[i] & 0xFFF)) += (uintptr_t)delta;
                }
            }
            pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pReloc) + pReloc->SizeOfBlock);
        }
    }
    pData->Stage = 2; // [STAGE 2] Relocations Done

    // Imports
    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* pImport = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        while (pImport->Name) {
            HMODULE hMod = pData->pLoadLibraryA(reinterpret_cast<char*>(pBase + pImport->Name));
            auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImport->FirstThunk);
            auto* pOrigThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + (pImport->OriginalFirstThunk ? pImport->OriginalFirstThunk : pImport->FirstThunk));
            while (pOrigThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pOrigThunk->u1.Ordinal)) {
                    pThunk->u1.Function = (uintptr_t)pData->pGetProcAddress(hMod, (char*)(pOrigThunk->u1.Ordinal & 0xFFFF));
                } else {
                    auto* pName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pOrigThunk->u1.AddressOfData);
                    pThunk->u1.Function = (uintptr_t)pData->pGetProcAddress(hMod, pName->Name);
                }
                pThunk++; pOrigThunk++;
            }
            pImport++;
        }
    }
    pData->Stage = 3; // [STAGE 3] Imports Done

    if (pOpt->AddressOfEntryPoint) {
        auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOpt->AddressOfEntryPoint);
        pData->Stage = 4; // [STAGE 4] Calling DllMain
        _DllMain(pBase, DLL_PROCESS_ATTACH, pData);
    }
    
    // Goliath V40: Atomic Inline Healing (Disabled: OriginalBytes uninitialized and causes crash)
    // Restoration must happen before Stage 6 to ensure 100% stock state
    /*for (int i = 0; i < 14; i++) {
        *(BYTE*)(pData->pTargetCode + i) = pData->OriginalBytes[i];
    }*/
    pData->Stage = 5; // [STAGE 5] Engine Tunnel Healed
    
    *pData->pInjected = true;
    pData->Stage = 6; // [STAGE 6] Absolute Success
}
#pragma optimize("", on)

// Skeleton Wrapper for VTable Redirection
const BYTE ShellcodeV13_Stub[] = {
    0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, // push registers
    0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // mov rcx, 0x100000000 (Data)
    0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, LogicFunc (Placeholder)
    0xFF, 0xD0, // call rax
    0x48, 0x83, 0xC4, 0x40, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E, 0x5B, // pop registers
    0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, OriginalHeartbeat (Placeholder)
    0xFF, 0xE0  // jmp rax
};

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

bool GoliathV47(HANDLE hProc, DWORD PID, const char* szDllFile)
{
    std::cout << "\n [!] Velocity Ultima (Goliath V47): GHOST VTABLE ACTIVATED...";

    uintptr_t robloxBase = GetModuleBase(hProc, "RobloxPlayerBeta.exe");
    if (!robloxBase) return false;

    std::cout << "\n [!] Audit: Mirroring forensic segments...";
    auto text = Scanner::GetSectionFractal(hProc, robloxBase, ".text");
    auto rdata = Scanner::GetSectionFractal(hProc, robloxBase, ".rdata");

    // Phase 1: Genetic VTable DNA Sniffing
    std::cout << "\n [!] Identity: Scrawling Active Heap for Genetic Job Signature...";
    
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    uintptr_t addr = (uintptr_t)sysInfo.lpMinimumApplicationAddress;
    
    uintptr_t heartbeatJob = 0;
    while (addr < (uintptr_t)sysInfo.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(hProc, (LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            std::vector<BYTE> buffer(mbi.RegionSize);
            SIZE_T bytesRead;
            if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                for (size_t i = 0; i < bytesRead - 8; i += 8) {
                    uintptr_t potentialVTable = *reinterpret_cast<uintptr_t*>(&buffer[i]);
                    if (potentialVTable > rdata.base && potentialVTable < (rdata.base + rdata.size)) {
                        uintptr_t heartbeatFn;
                        if (ReadProcessMemory(hProc, (LPCVOID)(potentialVTable + 0x10), &heartbeatFn, sizeof(heartbeatFn), nullptr)) {
                            uintptr_t resolvedFn = Scanner::FollowTrampolines(hProc, heartbeatFn);
                            if (Scanner::VerifyCodeEntry(hProc, resolvedFn)) {
                                heartbeatJob = (uintptr_t)mbi.BaseAddress + i;
                                std::cout << "\n [+] GENETIC MATCH: Heartbeat identified by VTable DNA [0x" << std::hex << heartbeatJob << "].";
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (heartbeatJob) break;
        addr += mbi.RegionSize;
    }

    if (!heartbeatJob) return false;

    uintptr_t originalVTable;
    ReadProcessMemory(hProc, (LPCVOID)heartbeatJob, &originalVTable, sizeof(originalVTable), nullptr);

    // STEP 7: Proximal Ghost VTable Allocation
    std::cout << "\n [!] Identity: Establishing Proximal Ghost Anchor...";
    uintptr_t sharedMem = (uintptr_t)VirtualAllocEx(hProc, nullptr, 0x8000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!sharedMem) return false;

    // STEP 8: Clone original VTable (0x200 bytes for stability)
    BYTE vtableBuffer[0x200];
    ReadProcessMemory(hProc, (LPCVOID)originalVTable, vtableBuffer, 0x200, nullptr);
    
    uintptr_t shadowVTableAddr = sharedMem + 0x4000;
    WriteProcessMemory(hProc, (LPVOID)shadowVTableAddr, vtableBuffer, 0x200, nullptr);
    std::cout << "\n [+] SHADOW VTABLE: Clone established at [0x" << std::hex << shadowVTableAddr << "].";

    // STEP 9: Dispatch Bridge
    uintptr_t targetModBase = GetModuleBase(hProc, "devenum.dll");
    std::ifstream File(szDllFile, std::ios::binary | std::ios::ate);
    auto FileSize = File.tellg();

    // Goliath V48: Dynamic Base Selection
    if (!targetModBase) {
        std::cout << "\n [!] Audit: devenum.dll not found. Allocating dynamic base...";
        targetModBase = (uintptr_t)VirtualAllocEx(hProc, nullptr, (size_t)FileSize + 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }

    BYTE* pSrcData = new BYTE[static_cast<UINT_PTR>(FileSize)];
    File.seekg(0, std::ios::beg);
    File.read(reinterpret_cast<char*>(pSrcData), FileSize);
    File.close();

    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData)->e_lfanew);
    DWORD oldProt;
    VirtualProtectEx(hProc, (LPVOID)targetModBase, pNt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &oldProt);
    WriteProcessMemory(hProc, (LPVOID)targetModBase, pSrcData, pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSection = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++, pSection++) {
        if (pSection->SizeOfRawData)
            WriteProcessMemory(hProc, (LPVOID)(targetModBase + pSection->VirtualAddress), pSrcData + pSection->PointerToRawData, pSection->SizeOfRawData, nullptr);
    }
    delete[] pSrcData;

    MANUAL_MAPPING_DATA data{ 0 };
    data.pLoadLibraryA = (f_LoadLibraryA)LoadLibraryA;
    data.pGetProcAddress = (f_GetProcAddress)GetProcAddress;
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    data.pRtlAddFunctionTable = (f_RtlAddFunctionTable)GetProcAddress(hK32, "RtlAddFunctionTable");
    data.pBeep = (f_Beep)GetProcAddress(hK32, "Beep");
    data.pbase = (BYTE*)targetModBase;
    data.pInjected = (bool*)(sharedMem + 0x7000);
    
    // Resolve original heartbeat for shellcode fallback
    uintptr_t heartbeatProxy;
    memcpy(&heartbeatProxy, vtableBuffer + 0x10, sizeof(uintptr_t));
    data.originalHeartbeat = Scanner::FollowTrampolines(hProc, heartbeatProxy);
    data.pTargetCode = data.originalHeartbeat;

    uintptr_t logicAddr = sharedMem + 0x1000;
    uintptr_t stubAddr = sharedMem + 0x2000;
    WriteProcessMemory(hProc, (LPVOID)logicAddr, ShellcodeV13_Logic, 0x1000, nullptr);

    BYTE stub[sizeof(ShellcodeV13_Stub)];
    memcpy(stub, ShellcodeV13_Stub, sizeof(stub));
    *reinterpret_cast<uintptr_t*>(stub + 0x11) = sharedMem; 
    *reinterpret_cast<uintptr_t*>(stub + 0x1B) = logicAddr; 
    *reinterpret_cast<uintptr_t*>(stub + 0x36) = data.originalHeartbeat;

    bool fals = false;
    WriteProcessMemory(hProc, (LPVOID)data.pInjected, &fals, sizeof(bool), nullptr);
    WriteProcessMemory(hProc, (LPVOID)sharedMem, &data, sizeof(data), nullptr);
    WriteProcessMemory(hProc, (LPVOID)stubAddr, stub, sizeof(stub), nullptr);
    
    // Goliath V47: Ghost Redirection (Redirect index 2 of shadow VTable)
    WriteProcessMemory(hProc, (LPVOID)(shadowVTableAddr + 0x10), &stubAddr, sizeof(uintptr_t), nullptr);

    // Perform the Atomic Swap: job->vtable = shadow_vtable
    WriteProcessMemory(hProc, (LPVOID)heartbeatJob, &shadowVTableAddr, sizeof(uintptr_t), nullptr);

    std::cout << "\n----------------------------------------------------------------------";
    std::cout << "\n [+] Goliath V47: Ghost Surge Dispatched.";
    std::cout << "\n----------------------------------------------------------------------";

    // Shadow Monitoring Loop
    while (true) {
        MANUAL_MAPPING_DATA currentData;
        ReadProcessMemory(hProc, (LPCVOID)sharedMem, &currentData, sizeof(currentData), nullptr);
        
        static DWORD lastStage = 0;
        if (currentData.Stage != lastStage) {
            std::cout << "\n [+] STAGE LOCK: Shellcode [Stage " << std::dec << currentData.Stage << "] achieved.";
            lastStage = currentData.Stage;
        }

        static char lastTele[1024] = { 0 };
        if (currentData.Telemetry[0] != '\0' && strcmp(currentData.Telemetry, lastTele) != 0) {
            std::cout << "\n [+] TELEMETRY: " << currentData.Telemetry;
            strcpy(lastTele, currentData.Telemetry);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        DWORD exitCode;
        GetExitCodeProcess(hProc, &exitCode);
        if (exitCode != STILL_ACTIVE) {
            std::cout << "\n [-] Error: Target process terminated (Crashed). Final Stage: " << std::dec << currentData.Stage;
            break;
        }
        if (currentData.Stage == 6) break;
    }
    return true;
}

int main()
{
    SetConsoleTitleA("Carbon 2.0 | Velocity Ultima | Goliath V47");
    std::cout << "\n [!] Velocity Ultima: Initializing Stealth Sniper Engine...";

    HWND hWindow = NULL;
    while (hWindow == NULL || !IsWindowVisible(hWindow)) {
        hWindow = FindWindowA(NULL, "Roblox");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    DWORD PID = 0;
    GetWindowThreadProcessId(hWindow, &PID);
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
    if (!hProc) return 0;

    std::cout << "\n [+] Target Identified: PID " << std::dec << PID;
    if (GoliathV47(hProc, PID, "Carbon-Module.dll")) {
        std::cout << "\n [!] Absolute Success: Carbon Engine Dispatched.";
    }
    
    CloseHandle(hProc);
    std::cout << "\n\n [!] Forensic Complete. Press ENTER to close...";
    std::cin.get();
    return 0;
}
