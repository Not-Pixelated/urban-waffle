#include <Luau/Bytecode.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/BytecodeUtils.h>
#include <Luau/Compiler.h>
#include <curl/curl.h>
#include <json.hpp>
#include <iostream>
#include "CarbonEnv.hpp"
#include "CarbonCore.hpp"
#include <algorithm>
#include <sstream>

namespace Carbon
{
    namespace Internal
    {
        lua_CFunction OriginalIndex = nullptr;
        lua_CFunction OriginalNamecall = nullptr;

        std::vector<const char*> UnsafeFunctions = {
            "Load", "OpenUrl", "OpenBrowserWindow", "ExecuteJavaScript", "OpenScreenshotsFolder",
            "HttpGet", "HttpGetAsync", "GetObjects", "PostAsync", "GetAsync", "RequestAsync"
        };

        static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
        {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        }

        std::string RawGet(std::string Url)
        {
            CURL* curl = curl_easy_init();
            std::string readBuffer;
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, Url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "Roblox/WinInet");
                curl_easy_perform(curl);
                curl_easy_cleanup(curl);
            }
            return readBuffer;
        }
    }

    namespace Execution
    {
        class BytecodeEncoder : public Luau::BytecodeEncoder
        {
            void encode(uint32_t* data, size_t count) override
            {
                const auto LookupTable = reinterpret_cast<BYTE*>(Offsets::OpcodeLookupTable);
                for (size_t i = 0; i < count;)
                {
                    uint8_t Opcode = LUAU_INSN_OP(data[i]);
                    uint8_t FinalOpcode = (uint8_t)(Opcode * 227);
                    FinalOpcode = LookupTable[FinalOpcode];

                    data[i] = (FinalOpcode) | (data[i] & ~0xFF);
                    i += Luau::getOpLength(static_cast<LuauOpcode>(Opcode));
                }
            }
        };

        std::string Compile(std::string Source)
        {
            std::cout << " [Carbon 2.0] Compiler: Generating bytecode (" << Source.size() << " bytes)...\n";

            auto BytecodeEncoding = BytecodeEncoder();
            static const char* CommonGlobals[] = { "Game", "Workspace", "game", "plugin", "script", "shared", "workspace", "_G", "_ENV", nullptr };

            Luau::CompileOptions Options;
            Options.debugLevel = 1;
            Options.optimizationLevel = 1;
            Options.mutableGlobals = CommonGlobals;
            Options.vectorLib = "Vector3";
            Options.vectorCtor = "new";
            Options.vectorType = "Vector3";

            std::string Result = Luau::compile(Source, Options, {}, &BytecodeEncoding);
            std::cout << " [Carbon 2.0] Compiler: Success. Binary size: " << Result.size() << " bytes.\n";
            return Result;
        }

        void Execute(lua_State* L, std::string Script)
        {
            if (!L || Script.empty()) {
                std::cout << " [-] Execution Error: Null state or empty source.\n";
                return;
            }

            int OriginalTop = lua_gettop(L);
            lua_State* ExecutionThread = lua_newthread(L);
            lua_pop(L, 1);

            std::cout << " [+] Execution: Thread spawned (" << (void*)ExecutionThread << "). Sandboxing...\n";
            luaL_sandboxthread(ExecutionThread);
            Core::SetThreadCapabilities(ExecutionThread, 8, Internal::MaxCapabilities);

            std::string Bytecode = Compile(Script);
            if (luau_load(ExecutionThread, "", Bytecode.c_str(), Bytecode.length(), 0) != LUA_OK)
            {
                std::cout << " [-] Compiler Error: " << lua_tostring(ExecutionThread, -1) << "\n";
                lua_pop(ExecutionThread, 1);
            }
            else
            {
                Closure* cl = (Closure*)luaA_toobject(ExecutionThread, -1);
                Core::SetProtoCapabilities(cl->l.p, Internal::MaxCapabilities);

                lua_getglobal(ExecutionThread, "task");
                lua_getfield(ExecutionThread, -1, "defer");
                lua_remove(ExecutionThread, -2);
                lua_insert(ExecutionThread, -2);

                std::cout << " [!] Execution: Deferring task to Luau scheduler...\n";
                if (lua_pcall(ExecutionThread, 1, 0, 0) != LUA_OK)
                {
                    std::cout << " [-] Runtime Error: " << lua_tostring(ExecutionThread, -1) << "\n";
                    lua_pop(ExecutionThread, 1);
                }
                else {
                    std::cout << " [+] Execution: Script successfully scheduled.\n";
                }
            }
            lua_settop(L, OriginalTop);
        }

        void HookRenderStepped(lua_State* L)
        {
            std::lock_guard<std::mutex> Lock(Internal::QueueMutex);
            while (!Internal::ExecutionRequests.empty())
            {
                std::string Script = Internal::ExecutionRequests.front();
                Internal::ExecutionRequests.erase(Internal::ExecutionRequests.begin());
                Execute(L, Script);
            }
        }
    }

    namespace Environment
    {
        namespace Hooks
        {
            int Index(lua_State* L)
            {
                if (L->userdata->Capabilities == Internal::MaxCapabilities)
                {
                    std::string Key = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
                    for (const char* Function : Internal::UnsafeFunctions)
                    {
                        if (Key == Function)
                        {
                            luaL_error(L, "Blocked: Function '%s' is restricted for security reasons", Function);
                            return 0;
                        }
                    }

                    if (Key == "HttpGet" || Key == "HttpGetAsync")
                    {
                        lua_pushcclosure(L, Libraries::Http::HttpGet, "HttpGet", 0);
                        return 1;
                    }
                    else if (Key == "GetObjects")
                    {
                        lua_pushcclosure(L, Libraries::Misc::GetObjects, "GetObjects", 0);
                        return 1;
                    }
                }
                return Internal::OriginalIndex(L);
            }

            int Namecall(lua_State* L)
            {
                if (L->userdata->Capabilities == Internal::MaxCapabilities)
                {
                    std::string Key = L->namecall->data;
                    for (const char* Function : Internal::UnsafeFunctions)
                    {
                        if (Key == Function)
                        {
                            luaL_error(L, "Blocked: Function '%s' is restricted for security reasons", Function);
                            return 0;
                        }
                    }

                    if (Key == "HttpGet" || Key == "HttpGetAsync")
                    {
                        return Libraries::Http::HttpGet(L);
                    }
                    else if (Key == "GetObjects")
                    {
                        return Libraries::Misc::GetObjects(L);
                    }
                }
                return Internal::OriginalNamecall(L);
            }

            void Setup(lua_State* L)
            {
                int OriginalTop = lua_gettop(L);
                lua_getglobal(L, "game");
                
                luaL_getmetafield(L, -1, "__index");
                if (lua_type(L, -1) == LUA_TFUNCTION)
                {
                    Closure* IndexClosure = (Closure*)clvalue(luaA_toobject(L, -1));
                    Internal::OriginalIndex = IndexClosure->c.f;
                    IndexClosure->c.f = Index;
                }
                lua_pop(L, 1);

                luaL_getmetafield(L, -1, "__namecall");
                if (lua_type(L, -1) == LUA_TFUNCTION)
                {
                    Closure* NamecallClosure = (Closure*)clvalue(luaA_toobject(L, -1));
                    Internal::OriginalNamecall = NamecallClosure->c.f;
                    NamecallClosure->c.f = Namecall;
                }
                lua_pop(L, 1);
                lua_settop(L, OriginalTop);
            }
        }

        namespace Libraries
        {
            namespace Http
            {
                int HttpGet(lua_State* L)
                {
                    std::string Url = lua_tostring(L, lua_isstring(L, 1) ? 1 : 2);
                    if (Url.find("http://") != 0 && Url.find("https://") != 0)
                        luaL_error(L, "Invalid protocol (http/https expected)");

                    return Async::YieldExecution(L, [Url]() -> Async::ResumeResult
                    {
                        std::string Result = Internal::RawGet(Url);
                        return [Result](lua_State* L) -> int
                        {
                            if (!Result.empty())
                                lua_pushlstring(L, Result.data(), Result.size());
                            else
                                lua_pushnil(L);
                            return 1;
                        };
                    });
                }

                int request(lua_State* L)
                {
                    luaL_checktype(L, 1, LUA_TTABLE);
                    lua_getfield(L, 1, "Url");
                    std::string Url = lua_tostring(L, -1);
                    lua_pop(L, 1);

                    return Async::YieldExecution(L, [Url]() -> Async::ResumeResult
                    {
                        std::string Response = Internal::RawGet(Url);
                        return [Response](lua_State* L) -> int
                        {
                            lua_newtable(L);
                            lua_pushinteger(L, Response.empty() ? 400 : 200);
                            lua_setfield(L, -2, "StatusCode");
                            lua_pushlstring(L, Response.data(), Response.size());
                            lua_setfield(L, -2, "Body");
                            return 1;
                        };
                    });
                }

                void Register(lua_State* L)
                {
                    Utils::AddFunction(L, "HttpGet", HttpGet);
                    Utils::AddFunction(L, "request", request);
                    Utils::AddFunction(L, "http_request", request);
                    lua_newtable(L);
                    lua_pushcclosure(L, request, "request", 0);
                    lua_setfield(L, -2, "request");
                    lua_setglobal(L, "http");
                }
            }

            namespace Closures
            {
                int loadstring(lua_State* L)
                {
                    const char* Source = luaL_checkstring(L, 1);
                    const char* Name = luaL_optstring(L, 2, "@Carbon");
                    std::string Bytecode = Execution::Compile(Source);
                    if (luau_load(L, Name, Bytecode.data(), Bytecode.size(), 0) != LUA_OK)
                    {
                        lua_pushnil(L);
                        lua_pushvalue(L, -2);
                        return 2;
                    }
                    Closure* cl = (Closure*)clvalue(luaA_toobject(L, -1));
                    Core::SetProtoCapabilities(cl->l.p, Internal::MaxCapabilities);
                    return 1;
                }

                void Register(lua_State* L)
                {
                    Utils::AddFunction(L, "loadstring", loadstring);
                }
            }

            namespace Misc
            {
                int GetObjects(lua_State* L)
                {
                    std::string Id = luaL_checkstring(L, 1);
                    // Match yub logic: return an empty table for now or implement as bridge
                    lua_newtable(L);
                    return 1;
                }

                void Register(lua_State* L)
                {
                    Utils::AddFunction(L, "GetObjects", GetObjects);
                }
            }
        }

        void Setup(lua_State* L)
        {
            luaL_sandboxthread(L);
            Libraries::Closures::Register(L);
            Libraries::Http::Register(L);
            Libraries::Misc::Register(L);
            Hooks::Setup(L);
            
            lua_newtable(L); lua_setglobal(L, "_G");
            lua_newtable(L); lua_setglobal(L, "shared");
        }
    }
}
