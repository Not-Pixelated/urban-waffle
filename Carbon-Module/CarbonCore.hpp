#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <functional>

#include <lua.h>
#include <lualib.h>
#include <lstate.h>
#include <lobject.h>
#include <lapi.h>

#define REBASE(Address) (Address + reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))

namespace Carbon
{
    namespace Internal
    {
        inline uintptr_t LastDataModel = 0;
        inline lua_State* ExploitThread = nullptr;
        inline std::vector<std::string> ExecutionRequests;
        inline std::queue<std::function<void()>> YieldQueue;
        inline std::mutex QueueMutex;
        inline uintptr_t MaxCapabilities = 0xFFFFFFFFFFFFFFFF;
        inline void* IPCData = nullptr;
    }

    namespace Offsets
    {
        // Internal Reference: client_offsets.txt
        inline const uintptr_t Print = REBASE(0x1CD0600);
        inline const uintptr_t FakeDataModelPointer = REBASE(0x795A0D8);
        constexpr uintptr_t FakeDataModelToDataModel = 0x1C0;

        inline const uintptr_t GetLuaStateForInstance = REBASE(0x1B19950);
        constexpr uintptr_t RequireBypass = 0x8FD;
        
        inline const uintptr_t OpcodeLookupTable = REBASE(0x61ed730);
        inline const uintptr_t TaskDefer = REBASE(0x1CB7290);
        inline const uintptr_t ScriptContextResume = REBASE(0x1C02930);
        constexpr uintptr_t ScriptContextToResume = 0x10; // Placeholder until verified, YuB uses 0x10
    }

    namespace Core
    {
        uintptr_t GetDataModel();
        lua_State* GetLuaState(uintptr_t Instance);
        void SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities);
        void SetProtoCapabilities(Proto* p, uintptr_t Capabilities);
        bool Initialize(uintptr_t DataModel);
    }

    namespace Async
    {
        struct YieldState { int unused; }; // Target-specific struct
        using ResumeResult = std::function<int(lua_State*)>;

        void RunYield();
        int YieldExecution(lua_State* L, const std::function<ResumeResult()>& YieldingClosure);
    }

    namespace Utils
    {
        void AddFunction(lua_State* L, const char* Name, lua_CFunction Func);
    }
}
