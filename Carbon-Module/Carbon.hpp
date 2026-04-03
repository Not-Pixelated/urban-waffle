#pragma once

#include "CarbonCore.hpp"
#include "CarbonEnv.hpp"
#include "CarbonIPC.hpp"

namespace Carbon
{
    // High-level Carbon Management
    inline void Initialize()
    {
        IPC::Initialize();
    }
}
