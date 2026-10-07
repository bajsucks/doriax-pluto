// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ScriptCompiler.h"

// Never include lua.hpp/lua.h directly: Pluto's luaconf.h leaks generic ANSI
// colour macros (RED, RESET, ...) that collide with engine enums. PlutoLua.h
// includes Pluto and drops them.
#include "PlutoLua.h"

#include <cctype>
#include <cstdlib>
#include <mutex>

namespace doriax::editor {

namespace {

    // Bytecode is version-locked to the producing Pluto build. The revision is
    // surfaced in export metadata and diagnostics so a mismatch is diagnosable
    // instead of a bare "version mismatch" at load time (W5.4).
    std::mutex& compilerMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // One state for the whole process: luaL_newstate + luaL_openselectedlibs is
    // expensive, and compiling a project means one call per script. The runtime
    // opens the same library set, so a chunk that loads here loads there.
    //
    // Never closed on purpose: it outlives every caller and a static destructor
    // would race the process teardown.
    lua_State* scratchState() {
        static lua_State* state = nullptr;
        if (!state) {
            state = luaL_newstate();
            if (state) {
                luaL_openselectedlibs(state, ~0, 0);
            }
        }
        return state;
    }

    // Loader messages look like "chunkname:12: unexpected symbol"; the chunk
    // name may itself contain colons ("lua://main.pluto", "C:\..."), so scan for
    // the first ":<digits>:" run instead of splitting on the first colon.
    int parseErrorLine(const std::string& message) {
        size_t pos = 0;
        while ((pos = message.find(':', pos)) != std::string::npos) {
            size_t i = pos + 1;
            const size_t start = i;
            while (i < message.size() && std::isdigit(static_cast<unsigned char>(message[i]))) i++;
            if (i > start && i < message.size() && message[i] == ':') {
                return std::atoi(message.substr(start, i - start).c_str());
            }
            pos++;
        }
        return 0;
    }

    std::string stringAtTop(lua_State* L, const char* fallback) {
        const char* text = lua_tostring(L, -1);
        return text ? std::string(text) : std::string(fallback);
    }

    struct DumpBuffer {
        std::string bytes;
    };

    int dumpWriter(lua_State* /*L*/, const void* data, size_t size, void* userData) {
        static_cast<DumpBuffer*>(userData)->bytes.append(static_cast<const char*>(data), size);
        return 0;
    }

    ScriptCompiler::Result loadChunk(const std::string& source, const std::string& chunkName) {
        ScriptCompiler::Result result;

        std::lock_guard<std::mutex> lock(compilerMutex());

        lua_State* L = scratchState();
        if (!L) {
            result.error = "Could not create a Pluto state to compile with";
            return result;
        }

        const int base = lua_gettop(L);
        // Mode "t": a text chunk only. A binary blob misnamed .pluto fails here
        // rather than being silently accepted (D3).
        if (luaL_loadbufferx(L, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK) {
            result.error = stringAtTop(L, "unknown Pluto parse error");
            result.line = parseErrorLine(result.error);
            lua_settop(L, base);
            return result;
        }

        result.ok = true;
        lua_settop(L, base);
        return result;
    }

} // namespace

const char* ScriptCompiler::runtimeVersion() {
    return PLUTO_VERSION;
}

const char* ScriptCompiler::bytecodeExtension() {
    return ".luac";
}

ScriptCompiler::Result ScriptCompiler::checkSyntax(const std::string& source, const std::string& chunkName) {
    return loadChunk(source, chunkName);
}

ScriptCompiler::Result ScriptCompiler::compile(const std::string& source, const std::string& chunkName, bool strip) {
    ScriptCompiler::Result result;

    std::lock_guard<std::mutex> lock(compilerMutex());

    lua_State* L = scratchState();
    if (!L) {
        result.error = "Could not create a Pluto state to compile with";
        return result;
    }

    const int base = lua_gettop(L);
    if (luaL_loadbufferx(L, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK) {
        result.error = stringAtTop(L, "unknown Pluto parse error");
        result.line = parseErrorLine(result.error);
        lua_settop(L, base);
        return result;
    }

    DumpBuffer buffer;
    if (lua_dump(L, dumpWriter, &buffer, strip ? 1 : 0) != 0) {
        result.error = "Pluto could not dump bytecode for " + chunkName;
        lua_settop(L, base);
        return result;
    }

    lua_settop(L, base);
    result.ok = true;
    result.bytecode = std::move(buffer.bytes);
    return result;
}

}
