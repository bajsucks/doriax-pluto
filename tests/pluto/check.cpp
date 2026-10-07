// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT
//
// Headless smoke check for the script runtime (W7.1). No window and no editor:
// it links the same `pluto` library the engine links, so it fails when the
// pinned revision stops parsing Pluto syntax, when a standard-library extension
// disappears, or when the bytecode dumper stops producing a loadable chunk.
//
// lua.hpp is included directly on purpose: this file is outside engine/ and
// editor/, so the PlutoLua.h macro shim (which exists for engine enums) is not
// relevant here, and the test should depend on nothing but Pluto itself.

#include "lua.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (condition) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

// Exercises the Pluto additions the fork relies on: classes and inheritance,
// switch/case/default, enums, and the extended standard library.
const char* kSource = R"(
class Animal function speak() return "hi" end end
class Dog extends Animal function speak() return "woof" end end
local animal = new Dog()
switch 2 do case 1: error("switch took case 1") break case 2: break default: error("switch hit default") end
enum Color begin RED, GREEN end
RESULT_SPEAK = animal:speak()
RESULT_RED = RED
RESULT_GREEN = GREEN
RESULT_JSON = json.encode({answer = 42})
)";

struct DumpBuffer {
    std::string bytes;
};

int dumpWriter(lua_State* /*L*/, const void* data, size_t size, void* userData) {
    static_cast<DumpBuffer*>(userData)->bytes.append(static_cast<const char*>(data), size);
    return 0;
}

bool runChunk(lua_State* L, const void* data, size_t size, const char* name, const char* mode) {
    if (luaL_loadbufferx(L, static_cast<const char*>(data), size, name, mode) != LUA_OK) {
        std::printf("FAIL loading %s: %s\n", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::printf("FAIL running %s: %s\n", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void expectGlobals(lua_State* L, const char* label) {
    lua_getglobal(L, "RESULT_SPEAK");
    check(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "woof",
          (std::string(label) + ": class inheritance and method dispatch").c_str());
    lua_pop(L, 1);

    lua_getglobal(L, "RESULT_RED");
    const lua_Integer red = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getglobal(L, "RESULT_GREEN");
    const lua_Integer green = lua_tointeger(L, -1);
    lua_pop(L, 1);
    check(red == 1 && green == 2, (std::string(label) + ": enum begin block").c_str());

    lua_getglobal(L, "RESULT_JSON");
    const bool jsonOk = lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "{\"answer\":42}";
    lua_pop(L, 1);
    check(jsonOk, (std::string(label) + ": json extension").c_str());
}

} // namespace

int main() {
    lua_State* L = luaL_newstate();
    if (!L) {
        std::printf("FAIL could not create a Pluto state\n");
        return 1;
    }
    // Must match LuaBinding::registerClasses: ~0 opens every library, so the
    // test covers the same surface scripts get at runtime.
    luaL_openselectedlibs(L, ~0, 0);

    check(std::string(PLUTO_VERSION).rfind("Pluto", 0) == 0, "PLUTO_VERSION is a Pluto revision");

    if (runChunk(L, kSource, std::strlen(kSource), "smoke.pluto", "t")) {
        expectGlobals(L, "source");
    }

    // Bytecode round trip: dump with the same lua_dump the editor's
    // ScriptCompiler uses, then load it back as bytecode only.
    if (luaL_loadbufferx(L, kSource, std::strlen(kSource), "smoke.pluto", "t") == LUA_OK) {
        DumpBuffer buffer;
        if (lua_dump(L, dumpWriter, &buffer, 1) == 0 && !buffer.bytes.empty()) {
            check(true, "lua_dump produced bytecode");
            // The globals from the source run are still set; clear them so the
            // bytecode run has to set them itself.
            lua_pushnil(L); lua_setglobal(L, "RESULT_SPEAK");
            lua_pushnil(L); lua_setglobal(L, "RESULT_RED");
            lua_pushnil(L); lua_setglobal(L, "RESULT_GREEN");
            lua_pushnil(L); lua_setglobal(L, "RESULT_JSON");

            if (runChunk(L, buffer.bytes.data(), buffer.bytes.size(), "smoke.luac", "b")) {
                expectGlobals(L, "bytecode");
            }
        } else {
            check(false, "lua_dump produced bytecode");
        }
    } else {
        check(false, "reloading the source for a bytecode dump");
        lua_pop(L, 1);
    }

    // A binary chunk must not load as text, which is what lets the engine pick
    // a loader mode from the extension (D3).
    {
        luaL_loadbufferx(L, kSource, std::strlen(kSource), "smoke.pluto", "t");
        DumpBuffer buffer;
        lua_dump(L, dumpWriter, &buffer, 1);
        const int status = luaL_loadbufferx(L, buffer.bytes.data(), buffer.bytes.size(), "wrong.lua", "t");
        check(status != LUA_OK, "bytecode is rejected when loaded in text-only mode");
        lua_settop(L, 0);
    }

    lua_close(L);
    std::printf(g_failures == 0 ? "PLUTO_SMOKE_OK\n" : "PLUTO_SMOKE_FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
