#pragma once

#include <string>
#include <vector>
#include <map>
#include <Dependencies/Luau/VM/include/lua.h>
#include <Dependencies/Luau/VM/src/lstate.h>
#include "CarbonCore.hpp"

namespace Carbon
{
    namespace Environment
    {
        void Initialize(lua_State* L);
        void Setup(lua_State* L);
        
        namespace Libraries
        {
            namespace Http
            {
                int HttpGet(lua_State* L);
                int request(lua_State* L);
                void Register(lua_State* L);
            }

            namespace Closures
            {
                int loadstring(lua_State* L);
                void Register(lua_State* L);
            }

            namespace Misc
            {
                int GetObjects(lua_State* L);
                void Register(lua_State* L);
            }
        }

        namespace Hooks
        {
            int Index(lua_State* L);
            int Namecall(lua_State* L);
            void Setup(lua_State* L);
        }
    }

    namespace Execution
    {
        std::string Compile(std::string Source);
        void Execute(lua_State* L, std::string Script);
        void HookRenderStepped(lua_State* L);
    }
}
