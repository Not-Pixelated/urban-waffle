#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <iostream>

namespace Scanner {
    inline uintptr_t Scan(HANDLE hProc, uintptr_t start, uintptr_t size, const char* signature) {
        auto pattern_to_byte = [](const char* pattern) {
            auto bytes = std::vector<int>{};
            auto start = const_cast<char*>(pattern);
            auto end = const_cast<char*>(pattern) + strlen(pattern);
            for (auto curr = start; curr < end; ++curr) {
                if (*curr == '?') {
                    ++curr;
                    if (*curr == '?') ++curr;
                    bytes.push_back(-1);
                } else {
                    bytes.push_back(strtoul(curr, &curr, 16));
                }
            }
            return bytes;
        };

        auto patternBytes = pattern_to_byte(signature);
        auto s = patternBytes.size();
        auto d = patternBytes.data();

        std::vector<BYTE> memory(size);
        SIZE_T bytesRead;
        if (!ReadProcessMemory(hProc, (LPCVOID)start, memory.data(), size, &bytesRead)) return 0;

        for (unsigned long i = 0; i < size - s; ++i) {
            bool found = true;
            for (unsigned long j = 0; j < s; ++j) {
                if (d[j] != -1 && memory[i + j] != d[j]) {
                    found = false;
                    break;
                }
            }
            if (found) return start + i;
        }
        return 0;
    }

    // Goliath V16: Forensic String Sniper - Renamed to avoid collision/ambiguity
    inline uintptr_t ScanStringForensic(HANDLE hProc, uintptr_t moduleBase, uintptr_t moduleSize, const char* str) {
        std::vector<BYTE> bytes(str, str + strlen(str) + 1);
        for (uintptr_t i = 0; i < moduleSize - bytes.size(); i += 8) {
            BYTE buf[32];
            SIZE_T bytesRead;
            if (ReadProcessMemory(hProc, (LPCVOID)(moduleBase + i), buf, bytes.size(), &bytesRead)) {
                if (memcmp(buf, bytes.data(), bytes.size()) == 0) {
                    return moduleBase + i;
                }
            }
        }
        return 0;
    }

    // Find all 8-byte pointer references to a target address
    inline std::vector<uintptr_t> ScanPointer(HANDLE hProc, uintptr_t start, uintptr_t size, uintptr_t target) {
        std::vector<uintptr_t> results;
        std::vector<BYTE> memory(size);
        SIZE_T bytesRead;
        if (!ReadProcessMemory(hProc, (LPCVOID)start, memory.data(), size, &bytesRead)) return results;

        for (unsigned long i = 0; i < size - 8; i += 8) {
            uintptr_t val = *reinterpret_cast<uintptr_t*>(memory.data() + i);
            if (val == target) results.push_back(start + i);
        }
        return results;
    }

    // Scan for a Structural VTable DNA: An array of pointers leading to a single module
    inline uintptr_t ScanVTableDNA(HANDLE hProc, uintptr_t start, uintptr_t size, uintptr_t moduleBase, uintptr_t moduleSize) {
        std::vector<BYTE> memory(size);
        SIZE_T bytesRead;
        if (!ReadProcessMemory(hProc, (LPCVOID)start, memory.data(), size, &bytesRead)) return 0;

        for (unsigned long i = 0; i < size - 0x100; i += 8) {
            uintptr_t p1 = *reinterpret_cast<uintptr_t*>(memory.data() + i);
            uintptr_t p2 = *reinterpret_cast<uintptr_t*>(memory.data() + i + 8);
            
            // Heuristic: Two pointers in a row must be within the same module's .text section
            if (p1 > moduleBase && p1 < (moduleBase + moduleSize) &&
                p2 > moduleBase && p2 < (moduleBase + moduleSize)) {
                
                // Possible job list found in TaskScheduler
                return start + i - 0x100; // Backtrack to find the struct root
            }
        }
        return 0;
    }

    // Read a 4-byte value from the process (used for pulse auditing)
    inline uint32_t ReadDword(HANDLE hProc, uintptr_t addr) {
        uint32_t val = 0;
        ReadProcessMemory(hProc, (LPCVOID)addr, &val, sizeof(uint32_t), nullptr);
        return val;
    }

    // Scan the entire process memory map (Global Scrawl)
    inline uintptr_t ScanGlobal(HANDLE hProc, const char* target) {
        size_t len = strlen(target);
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;

        while (addr < (uintptr_t)si.lpMaximumApplicationAddress) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQueryEx(hProc, (LPCVOID)addr, &mbi, sizeof(mbi))) break;

            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE) || (mbi.Protect & PAGE_READONLY) || (mbi.Protect & PAGE_EXECUTE_READWRITE)) {
                std::vector<BYTE> buffer(mbi.RegionSize);
                SIZE_T bytesRead;
                if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                    for (size_t i = 0; i < bytesRead - len; ++i) {
                        if (memcmp(buffer.data() + i, target, len) == 0) return (uintptr_t)mbi.BaseAddress + i;
                    }
                }
            }
            addr += mbi.RegionSize;
        }
        return 0;
    }

    // Scan the entire process heap for any Job VTable DNA point into the module code
    inline std::vector<uintptr_t> ScanVTablePhantom(HANDLE hProc, uintptr_t moduleBase, uintptr_t moduleSize) {
        std::vector<uintptr_t> results;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;

        while (addr < (uintptr_t)si.lpMaximumApplicationAddress) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQueryEx(hProc, (LPCVOID)addr, &mbi, sizeof(mbi))) break;

            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE)) {
                std::vector<BYTE> buffer(mbi.RegionSize);
                SIZE_T bytesRead;
                if (ReadProcessMemory(hProc, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                    for (size_t i = 0; i < bytesRead - 8; i += 8) {
                        uintptr_t vtable = *reinterpret_cast<uintptr_t*>(buffer.data() + i);
                        if (vtable > moduleBase && vtable < (moduleBase + moduleSize)) {
                            results.push_back((uintptr_t)mbi.BaseAddress + i);
                        }
                    }
                }
            }
            addr += mbi.RegionSize;
        }
        return results;
    }

    // Verify if a pointer is a legitimate virtual function table (Purification)
    inline bool IsValidVTable(HANDLE hProc, uintptr_t vtable, uintptr_t moduleBase, uintptr_t moduleSize) {
        if (vtable % 8 != 0) return false; // Must be aligned
        
        uintptr_t firstFour[4];
        if (!ReadProcessMemory(hProc, (LPCVOID)vtable, firstFour, sizeof(firstFour), nullptr)) return false;

        for (int i = 0; i < 4; i++) {
            if (firstFour[i] < moduleBase || firstFour[i] > (moduleBase + moduleSize)) return false;
        }
        return true;
    }

    // Goliath V41: Recursive Trampoline Follower (Neutron Scanner)
    inline uintptr_t FollowTrampolines(HANDLE hProc, uintptr_t addr) {
        for (int i = 0; i < 8; i++) { // Max deep-proxies: 8
            BYTE instr[14];
            if (!ReadProcessMemory(hProc, (LPCVOID)addr, instr, 14, nullptr)) break;

            if (instr[0] == 0xE9) { // jmp rel32
                int relative = *reinterpret_cast<int*>(instr + 1);
                addr = addr + 5 + relative;
            } else if (instr[0] == 0xEB) { // jmp rel8 (short)
                signed char relative = *reinterpret_cast<signed char*>(instr + 1);
                addr = addr + 2 + relative;
            } else if (instr[0] == 0x90 || (instr[0] == 0x48 && instr[1] == 0x90)) { // nop or rex-nop
                addr += (instr[0] == 0x48 ? 2 : 1);
            } else if (instr[0] == 0xFF && instr[1] == 0x25) { // jmp qword ptr [rip+rel32]
                int relative = *reinterpret_cast<int*>(instr + 2);
                uintptr_t ptr = 0;
                if (ReadProcessMemory(hProc, (LPCVOID)(addr + 6 + relative), &ptr, sizeof(ptr), nullptr)) {
                    addr = ptr;
                } else break;
            } else break;
        }
        return addr;
    }

    // Goliath V45: Proximal Memory Scanner
    inline uintptr_t FindFreeMemoryNearRegion(HANDLE hProc, uintptr_t target, size_t size) {
        uintptr_t start = (target > 0x7FFFFFFF) ? (target - 0x7FFFFFFF) : 0x10000;
        uintptr_t end = target + 0x7FFFFFFF;

        MEMORY_BASIC_INFORMATION mbi;
        for (uintptr_t curr = start; curr < end; ) {
            if (!VirtualQueryEx(hProc, (LPCVOID)curr, &mbi, sizeof(mbi))) break;
            if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
                return curr;
            }
            curr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
        return 0;
    }

    // Goliath V40: Code DNA Sniffer (Prologue Verification)
    inline bool VerifyCodeEntry(HANDLE hProc, uintptr_t addr) {
        if (addr % 8 != 0 && addr % 16 != 0) return false;
        BYTE buf[4];
        if (!ReadProcessMemory(hProc, (LPCVOID)addr, buf, 4, nullptr)) return false;
        // Common 64-bit prologues: push rbp (55), sub rsp (48 83 EC), mov [rsp+8], rbx (48 89 5C 24), push rbx (40 53)
        if (buf[0] == 0x40 || buf[0] == 0x48 || buf[0] == 0x55 || buf[0] == 0x4C) return true;
        return false;
    }

    // Advanced: Find absolute address from relative offset (RIP-relative)
    inline uintptr_t ResolveRelative(HANDLE hProc, uintptr_t address, int offset, int instrSize) {
        int relative = 0;
        if (!ReadProcessMemory(hProc, (LPCVOID)(address + offset), &relative, sizeof(int), nullptr)) return 0;
        return address + instrSize + relative;
    }

    // Goliath V21: Linear Fractal Mirroring
    struct PageFragment { uintptr_t address; std::vector<BYTE> data; };
    struct FractalBuffer { 
        uintptr_t base; 
        uintptr_t size; 
        std::vector<BYTE> linearData; 
        std::vector<uintptr_t> remoteMap; 

        // Map local linear index to remote address in O(1)
        uintptr_t GetRemote(size_t linearIdx) const {
            if (linearIdx >= remoteMap.size()) return 0;
            return remoteMap[linearIdx];
        }
    };
    
    inline FractalBuffer GetSectionFractal(HANDLE hProc, uintptr_t moduleBase, const char* sectionName) {
        IMAGE_DOS_HEADER dosHeader;
        IMAGE_NT_HEADERS ntHeaders;
        if (!ReadProcessMemory(hProc, (LPCVOID)moduleBase, &dosHeader, sizeof(dosHeader), nullptr)) return { 0, 0, {}, {} };
        if (!ReadProcessMemory(hProc, (LPCVOID)(moduleBase + dosHeader.e_lfanew), &ntHeaders, sizeof(ntHeaders), nullptr)) return { 0, 0, {}, {} };

        IMAGE_SECTION_HEADER sectionHeader;
        uintptr_t sectionHeaderAddr = moduleBase + dosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS);

        for (int i = 0; i < ntHeaders.FileHeader.NumberOfSections; i++) {
            ReadProcessMemory(hProc, (LPCVOID)(sectionHeaderAddr + (i * sizeof(IMAGE_SECTION_HEADER))), &sectionHeader, sizeof(sectionHeader), nullptr);
            if (strcmp((const char*)sectionHeader.Name, sectionName) == 0) {
                FractalBuffer buffer{ moduleBase + sectionHeader.VirtualAddress, sectionHeader.Misc.VirtualSize, {}, {} };
                
                // Perform fractal audit (4KB steps) and linearize
                for (uintptr_t offset = 0; offset < buffer.size; offset += 0x1000) {
                    MEMORY_BASIC_INFORMATION mbi;
                    if (VirtualQueryEx(hProc, (LPCVOID)(buffer.base + offset), &mbi, sizeof(mbi))) {
                        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD)) {
                            std::vector<BYTE> page(0x1000);
                            SIZE_T bytesRead;
                            if (ReadProcessMemory(hProc, (LPCVOID)(buffer.base + offset), page.data(), 0x1000, &bytesRead)) {
                                size_t currentSize = buffer.linearData.size();
                                buffer.linearData.insert(buffer.linearData.end(), page.begin(), page.begin() + bytesRead);
                                for (size_t k = 0; k < bytesRead; k++) buffer.remoteMap.push_back(buffer.base + offset + k);
                            }
                        }
                    }
                }
                return buffer;
            }
        }
        return { 0, 0, {}, {} };
    }

    // Goliath V22: Linear Multi-Strand Forensic Sniper
    inline std::vector<uintptr_t> ScanStringFractalMulti(const FractalBuffer& fractal, const char* str) {
        std::vector<uintptr_t> results;
        std::vector<BYTE> bytes(str, str + strlen(str) + 1);
        for (size_t i = 0; i < fractal.linearData.size(); i++) {
            if (i + bytes.size() <= fractal.linearData.size()) {
                if (memcmp(fractal.linearData.data() + i, bytes.data(), bytes.size()) == 0) {
                    results.push_back(fractal.GetRemote(i));
                }
            }
        }
        return results;
    }

    // Goliath V22: Broad-Spectrum Linear Quantum Reference Sniper
    inline std::vector<uintptr_t> ScanStringReferencesFractal(const FractalBuffer& fractal, uintptr_t targetAddr) {
        std::vector<uintptr_t> refs;
        if (fractal.linearData.size() < 7) return refs;
        for (size_t i = 0; i < fractal.linearData.size() - 7; i++) {
            const BYTE* instr = &fractal.linearData[i];
            if ((instr[0] == 0x48 || instr[0] == 0x4C) && (instr[1] == 0x8D || instr[1] == 0x8B)) {
                if ((instr[2] & 0xC7) == 0x05) {
                    int relative = *reinterpret_cast<const int*>(instr + 3);
                    uintptr_t remote = fractal.GetRemote(i);
                    uintptr_t resolved = remote + 7 + relative;
                    if (resolved == targetAddr) refs.push_back(remote);
                }
            }
        }
        return refs;
    }

    // Goliath V24: Multidimensional Kinetic Auditor
    inline bool IsValidPointer(HANDLE hProc, uintptr_t ptr) {
        if (ptr < 0x10000 || ptr > 0x7FFFFFFFFFFF) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQueryEx(hProc, (LPCVOID)ptr, &mbi, sizeof(mbi))) {
            return (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD));
        }
        return false;
    }

    inline uintptr_t VerifyTaskSchedulerHeuristic(HANDLE hProc, uintptr_t ts) {
        if (!ts || !IsValidPointer(hProc, ts) || ts == 0x6165627472616548) return 0;
        
        static const int offsets[] = { 0x180, 0x190, 0x198, 0x1A0 };
        for (int offset : offsets) {
            uintptr_t jobVector[3]; // [Begin, End, EndCap]
            if (ReadProcessMemory(hProc, (LPCVOID)(ts + offset), jobVector, sizeof(jobVector), nullptr)) {
                if (jobVector[0] <= jobVector[1] && jobVector[1] <= jobVector[2] && IsValidPointer(hProc, jobVector[0])) {
                    size_t count = (jobVector[1] - jobVector[0]) / 8;
                    if (count >= 1 && count <= 250) return (uintptr_t)offset;
                }
            }
        }
        return 0;
    }

    // Goliath V24: Blind Heuristic Scrawl (DNA-Agnostic Recovery)
    inline uintptr_t BlindHeuristicScrawl(HANDLE hProc, const FractalBuffer& text, int& outOffset) {
        if (text.linearData.size() < 7) return 0;
        for (size_t i = 0; i < text.linearData.size() - 7; i++) {
            const BYTE* instr = &text.linearData[i];
            if ((instr[0] == 0x48 || instr[0] == 0x4C) && (instr[1] == 0x8B || instr[1] == 0x8D)) {
                if ((instr[2] & 0xC7) == 0x05) {
                    int relative = *reinterpret_cast<const int*>(instr + 3);
                    uintptr_t tsPtr = text.GetRemote(i + 7) + relative;
                    uintptr_t potentialTS = 0;
                    if (ReadProcessMemory(hProc, (LPCVOID)tsPtr, &potentialTS, sizeof(potentialTS), nullptr)) {
                        int foundOffset = (int)VerifyTaskSchedulerHeuristic(hProc, potentialTS);
                        if (foundOffset) {
                            outOffset = foundOffset;
                            return potentialTS;
                        }
                    }
                }
            }
        }
        return 0;
    }
    inline bool ReadRemoteBuffer(HANDLE hProc, uintptr_t ptr, void* buffer, size_t size) {
        if (!ptr || ptr < 0x10000) return false;
        return ReadProcessMemory(hProc, (LPCVOID)ptr, buffer, size, nullptr);
    }
}
