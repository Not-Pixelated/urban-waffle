#include "Carbon.hpp"
#include <thread>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "Proxy.h"

// Stealth Pipe Stream Redirector
class PipeBuffer : public std::streambuf {
public:
    int overflow(int char_to_log) override {
        if (char_to_log != EOF) {
            char c = (char)char_to_log;
            HANDLE hLogPipe = CreateFileA("\\\\.\\pipe\\CarbonLogs", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hLogPipe != INVALID_HANDLE_VALUE) {
                DWORD dwWritten;
                WriteFile(hLogPipe, &c, 1, &dwWritten, NULL);
                CloseHandle(hLogPipe);
            }
        }
        return char_to_log;
    }
};

PipeBuffer g_PipeBuf;
std::streambuf* g_OldBuf = nullptr;

void CarbonMain(void* pData)
{
    // Stealth initialization - Pass pData to IPC for shadow logging
    Carbon::Internal::IPCData = pData;
    Carbon::IPC::Log("Internal Engine: LOAD_SUCCESS. Stealth Diagnostic Bridge initializing...");
    OutputDebugStringA("[Carbon 2.0] Internal Telemetry Bridge Established.\n");
    
    std::cout << " [Carbon 2.0] Initializing IPC Bridge...\n";

    Carbon::IPC::Initialize();
    std::cout << " [Carbon 2.0] IPC Bridge Initialization Thread Dispatched.\n";

    while (true)
    {
        uintptr_t dataModel = 0;
        try {
            dataModel = Carbon::Core::GetDataModel();
        } catch (...) {
             std::cout << " [-] Core Error: Exception during DataModel discovery.\n";
        }

        if (dataModel && Carbon::Internal::LastDataModel != dataModel)
        {
            std::cout << " [+] New DataModel detected: " << std::hex << dataModel << ". Re-initializing environment...\n";
            Carbon::Internal::LastDataModel = dataModel;
            Carbon::Internal::ExecutionRequests.clear();

            lua_State* RobloxState = Carbon::Core::GetLuaState(dataModel);
            if (RobloxState)
            {
                std::cout << " [+] Found Roblox lua_State: " << (void*)RobloxState << ". Creating execution thread...\n";
                Carbon::Internal::ExploitThread = lua_newthread(RobloxState);
                
                std::cout << " [!] Elevating thread capabilities...\n";
                Carbon::Core::SetThreadCapabilities(Carbon::Internal::ExploitThread, 8, Carbon::Internal::MaxCapabilities);
                
                std::cout << " [!] Setting up Universal Naming Convention (UNC) environment...\n";
                Carbon::Environment::Setup(Carbon::Internal::ExploitThread);
                
                std::cout << " [!] Hooking RenderStepped execution pump...\n";
                Carbon::Execution::HookRenderStepped(Carbon::Internal::ExploitThread);

                std::cout << " --------------------------------------------------------\n";
                std::cout << "  Carbon 2.0 - Fully Modular Internal Engine Active\n";
                std::cout << " --------------------------------------------------------\n";
            }
            else {
                std::cout << " [-] Critical Error: Failed to acquire Roblox lua_State.\n";
            }
        }

        if (Carbon::Internal::ExploitThread)
            Carbon::Execution::HookRenderStepped(Carbon::Internal::ExploitThread);

        Carbon::Async::RunYield();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(CarbonMain, lpReserved).detach(); 
    }
    return TRUE;
}

// EXACT YUB-X PROTOTYPE
extern "C" __declspec(dllexport) int NextHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    return (int)CallNextHookEx(NULL, nCode, wParam, lParam);
}
