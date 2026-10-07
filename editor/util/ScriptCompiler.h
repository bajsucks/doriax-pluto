// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace doriax::editor {

    // Parses and precompiles Pluto/Lua scripts in-process (D4), using the same
    // pinned Pluto build the runtime links against. No subprocess, no external
    // `plutoc` binary to locate, and bytecode that always matches the engine.
    //
    // The scratch lua_State this uses is private to the compiler: nothing here
    // touches the live editor scripting VM, and no engine classes are
    // registered, so a chunk is parsed but never executed.
    class ScriptCompiler {
    public:
        struct Result {
            bool ok = false;
            // Filled by compile() only; the dumped Pluto bytecode.
            std::string bytecode;
            // Loader message, already carrying "<chunk>:<line>:" when it knows.
            std::string error;
            // 1-based line parsed out of error, 0 when the message has none.
            int line = 0;
        };

        // Parses the source and reports any syntax error. Source text only:
        // bytecode is an output format here, never an input.
        static Result checkSyntax(const std::string& source, const std::string& chunkName);

        // Parses then dumps the chunk as Pluto bytecode. strip=true drops local
        // names and debug info: smaller artifacts, worse stack traces (W5.6).
        static Result compile(const std::string& source, const std::string& chunkName, bool strip);

        // The runtime revision bytecode is locked to, e.g. "Pluto 0.12.2".
        static const char* runtimeVersion();

        // ".luac", the only extension the engine treats as bytecode.
        static const char* bytecodeExtension();
    };

}
