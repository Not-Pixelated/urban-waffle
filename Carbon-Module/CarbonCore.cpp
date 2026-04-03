#include "CarbonCore.hpp"
#include "CarbonIPC.hpp"
#include <iostream>
#include <iomanip>

namespace Carbon
{
    namespace Utils
    {
        void AddFunction(lua_State* L, const char* Name, lua_CFunction Func)
        {
            lua_pushcclosure(L, Func, Name, 0);
            lua_setglobal(L, Name);
        }
    }

    namespace Core
    {
        uintptr_t GetDataModel()
        {
            Carbon::IPC::Log("Attempting DataModel resolution from FakeDataModelPointer...");
            uintptr_t FakeDataModel = *reinterpret_cast<uintptr_t*>(::Offsets::DataModel::FakeDataModelPointer);
            
            if (!FakeDataModel) {
                Carbon::IPC::Log("[-] Core Error: FakeDataModel is NULL.");
                return 0;
            }

            uintptr_t DataModel = *reinterpret_cast<uintptr_t*>(FakeDataModel + ::Offsets::DataModel::FakeDataModelToDataModel);
            Carbon::IPC::Log("[+] Core: DataModel successfully resolved (DM: 0x%p)", (void*)DataModel);
            return DataModel;
        }

        lua_State* GetLuaState(uintptr_t Instance)
        {
            Carbon::IPC::Log("Bypassing Require check for Instance: 0x%p", (void*)Instance);
            *reinterpret_cast<BOOLEAN*>(Instance + ::Offsets::ExtraSpace::RequireBypass) = TRUE;

            Carbon::IPC::Log("Invoking GetLuaStateForInstance (Bridge)...");
            auto GetLuaStateForInstanceFunc = (lua_State*(__fastcall*)(uintptr_t, uint64_t*, uint64_t*))::Offsets::GetLuaStateForInstance;
            uint64_t Null = 0;
            lua_State* L = GetLuaStateForInstanceFunc(Instance, &Null, &Null);
            
            if (L)
                Carbon::IPC::Log("[+] Core: State acquisition successful (State: 0x%p).", (void*)L);
            else
                Carbon::IPC::Log("[-] Core Error: State acquisition FAILED.");

            return L;
        }

        void SetThreadCapabilities(lua_State* L, int Level, uintptr_t Capabilities)
        {
            if (L->userdata) {
                L->userdata->Identity = Level;
                L->userdata->Capabilities = Capabilities;
            }
        }

        void SetProtoCapabilities(Proto* p, uintptr_t Capabilities)
        {
            p->userdata = (void*)Capabilities;
            for (int i = 0; i < p->sizep; ++i)
                SetProtoCapabilities(p->p[i], Capabilities);
        }
    }

    namespace Async
    {
        void RunYield()
        {
            std::lock_guard<std::mutex> Lock(Internal::QueueMutex);
            if (!Internal::YieldQueue.empty())
            {
                auto YieldingRequest = Internal::YieldQueue.front();
                Internal::YieldQueue.pop();
                YieldingRequest();
            }
        }

        int YieldExecution(lua_State* L, const std::function<ResumeResult()>& YieldingClosure)
        {
            lua_pushthread(L);
            int YieldedThreadRef = lua_ref(L, -1);
            lua_pop(L, 1);

            std::thread([=]
            {
                ResumeResult ResumeFunction = YieldingClosure();

                std::lock_guard<std::mutex> Lock(Internal::QueueMutex);
                Internal::YieldQueue.emplace([=]() -> void
                {
                    auto ScriptContextResumeFunc = (int(__fastcall*)(uintptr_t, void*, lua_State**, int, int, int))::Offsets::ScriptContextResume;
                    
                    // Match yub logic: ScriptContext is from the extra space
                    uintptr_t ScriptContext = (uintptr_t)L->userdata->SharedExtraSpace->ScriptContext;
                    
                    // YieldState is ignored by Luau, match YuB's stack size
                    void* State = nullptr;
                    lua_State* Threads[] = { L };
                    
                    ScriptContextResumeFunc(ScriptContext + ::Offsets::ExtraSpace::ScriptContextToResume, State, Threads, ResumeFunction(L), 0, 0);

                    lua_unref(L, YieldedThreadRef);
                });
            }).detach();

            return lua_yield(L, 0);
        }
    }
}
