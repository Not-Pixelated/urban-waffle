#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include "CarbonIPC.hpp"
#include "CarbonCore.hpp"
#include <thread>
#include <vector>
#include <iostream>

namespace Carbon
{
    namespace IPC
    {
        void StartTcpServer(int Port)
        {
            auto PrintFunc = (uintptr_t(__fastcall*)(int, const char*, ...))Offsets::Print;
            WSADATA Wsa;
            if (WSAStartup(MAKEWORD(2, 2), &Wsa) != 0) {
                 std::cout << " [-] IPC Error: WSAStartup failed.\n";
                 return;
            }

            SOCKET ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (ListenSocket == INVALID_SOCKET) { WSACleanup(); return; }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(Port);

            if (bind(ListenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
            {
                std::cout << " [-] IPC Error: Port Bind failed (Port: " << Port << "). Close other exploit UIs.\n";
                closesocket(ListenSocket);
                WSACleanup();
                return;
            }

            if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
            {
                std::cout << " [-] IPC Error: Listen failure.\n";
                closesocket(ListenSocket);
                WSACleanup();
                return;
            }
            std::cout << " [+] IPC Bridge: Established TCP Server on Port " << Port << ".\n";

            while (true)
            {
                SOCKET Client = accept(ListenSocket, nullptr, nullptr);
                if (Client != INVALID_SOCKET)
                {
                    std::cout << " [+] IPC Bridge: Incoming connection. Recv header...\n";
                    uint32_t NetLen = 0;
                    if (recv(Client, (char*)&NetLen, sizeof(NetLen), 0) > 0)
                    {
                        uint32_t Len = ntohl(NetLen);
                        std::cout << " [+] IPC Bridge: Receiving payload (" << Len << " bytes)...\n";
                        if (Len > 0 && Len < 1024 * 1024 * 10) 
                        {
                            std::vector<char> buf(Len);
                            uint32_t rec = 0;
                            while (rec < Len)
                            {
                                int r = recv(Client, buf.data() + rec, Len - rec, 0);
                                if (r <= 0) break;
                                rec += r;
                            }
                            if (rec == Len)
                            {
                                std::string Script(buf.begin(), buf.end());
                                std::lock_guard<std::mutex> Lock(Internal::QueueMutex);
                                Internal::ExecutionRequests.push_back(Script);
                                std::cout << " [+] IPC Bridge: Script enqueued.\n";
                            }
                        }
                    }
                    closesocket(Client);
                }
            }
        }

        void StartPipeServer(std::string PipeName)
        {
            std::string FullPipeName = "\\\\.\\pipe\\" + PipeName;
            while (true)
            {
                HANDLE hPipe = CreateNamedPipeA(FullPipeName.c_str(),
                    PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                    PIPE_UNLIMITED_INSTANCES, 1024 * 64, 1024 * 64, 0, NULL);

                if (hPipe != INVALID_HANDLE_VALUE)
                {
                    if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
                    {
                        std::cout << " [+] IPC Bridge: Pipe client connected.\n";
                        char buffer[4096];
                        DWORD bytesRead;
                        std::string payload;

                        while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
                        {
                            buffer[bytesRead] = '\0';
                            payload += buffer;
                        }

                        if (!payload.empty())
                        {
                            std::cout << " [+] IPC Bridge: Script received via pipe.\n";
                            std::lock_guard<std::mutex> Lock(Internal::QueueMutex);
                            Internal::ExecutionRequests.push_back(payload);
                        }
                    }
                    DisconnectNamedPipe(hPipe);
                    CloseHandle(hPipe);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void Initialize()
        {
            std::thread(StartTcpServer, 3498).detach();
            std::thread(StartPipeServer, "CarbonExecution").detach();
        }

        void Log(const char* fmt, ...)
        {
            char buf[1024];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);

            // Relocatable Shadow Telemetry Bridge
            if (Carbon::Internal::IPCData)
            {
                // Telemetry starts at +0x3C in MANUAL_MAPPING_DATA
                char* TelemetryBuffer = (char*)((uintptr_t)Carbon::Internal::IPCData + 0x3C);
                
                // Clear and write immediately to shared memory
                memset(TelemetryBuffer, 0, 1024);
                strncpy(TelemetryBuffer, buf, 1023);
            }
            
            // Mirror to Debugger (DbgView)
            OutputDebugStringA(buf);
        }
    }
}
