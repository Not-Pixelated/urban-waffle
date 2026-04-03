#pragma once

#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace Carbon
{
    namespace IPC
    {
        void Initialize();
        void StartTcpServer(int Port);
        void StartPipeServer(std::string PipeName);
        void Log(const char* fmt, ...);
    }
}
