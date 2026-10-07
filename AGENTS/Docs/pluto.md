# Pluto integration

Status as of the current tree: **working on Linux (verified), unverified on
Windows/macOS/Android/iOS/Emscripten.** This page documents exactly how the
scripting runtime is wired, why, and what is still rough, so a future agent can
change it safely.

## TL;DR

- The script runtime is **Pluto 0.12.2**, a Lua 5.4 superset (its `lua.h`
  reports Lua **5.4.8**). Vanilla Lua 5.4.7 was removed.
- Build target: **`pluto`** (static), plus **`pluto_soup`** for the bundled
  "soup" support library that Pluto's standard library extensions depend on.
- Fetched with `FetchContent` from GitHub, pinned to the tag `0.12.2`
  (commit `bd484e23513d7e951ed3ec16b68a857afe070ecf`).
- Engine code must include **`engine/core/script/PlutoLua.h`**, never
  `lua.hpp` directly.
- The state is opened with
  `luaL_openselectedlibs(L, ~0, 0)` in
  `LuaBinding::registerClasses` (`engine/core/script/LuaBinding.cpp`).
- The editor's Lua keyword/autocomplete data was extended with Pluto keywords in
  `editor/window/widget/CustomTextEditor.cpp`.

## Version choice

| Pluto | Lua base | Notes |
| --- | --- | --- |
| 0.12.2 | 5.4.8 | **Pinned.** Last Lua-5.4 line; has `try`/`catch` + `global`; matches LuaBridge3. |
| 0.13.x | 5.5.1 | Not used. Dropped `try`/`catch` and `global`; Lua 5.5 C API; LuaBridge3 incompatible. |

The pin is deliberate, not lazy:

- The engine and its `libs/luabridge3` binding layer target Lua 5.4. Lua 5.5 is
  a different API surface.
- 0.12.2 is the closest match to the previous Lua 5.4.7, so the C API is stable.
- The editor keyword list requested for this fork (`class`, `switch`, `enum`,
  `try`, `catch`, `parent`, `export`) matches 0.12.x exactly — 0.13 removed some.

Upgrading Pluto means changing the tag **and** re-auditing the keyword list, the
macro shim (below), LuaBridge, and every caveat on this page.

## Build integration

`engine/libs/pluto/CMakeLists.txt` is a wrapper, because upstream ships Makefiles
and Visual Studio projects but no CMake build.

What it does:

- `FetchContent_Declare` + `FetchContent_MakeAvailable`. Upstream has no
  `CMakeLists.txt`, which `MakeAvailable` tolerates — the sources appear at
  `${pluto_SOURCE_DIR}`.
- **`pluto_soup`**: a static library built from
  `src/vendor/Soup/soup/*.cpp` (Pluto's crypto, JSON, XML, regex, networking,
  Bigint, etc. support code). Self-contained; no OpenSSL/zlib needed.
- **`pluto`**: a static library from `src/*.cpp` **minus** `lua.cpp` (the
  standalone REPL) and `luac.cpp` (the `plutoc` compiler).
- **`plutoc`**: an executable from `luac.cpp`, built on desktop platforms only
  (skipped for Android/iOS/Emscripten, where the editor does not run). It links
  `pluto`, uses the same `LUA_USE_*` define, and is added to the external-warning
  suppression in `engine/CMakeLists.txt`.
- PUBLIC **SYSTEM** include directories, so consumers get Pluto's headers
  without inheriting its warnings.
- `CXX_STANDARD 17` and `POSITION_INDEPENDENT_CODE ON` on both targets.
- Platform defines only on `pluto`: `LUA_USE_WINDOWS` / `LUA_USE_MACOSX` /
  `LUA_USE_LINUX` / `LUA_USE_IOS`.
- Link libraries, mirroring Pluto's Makefile:
  - Linux: `${CMAKE_DL_LIBS}` and `resolv` (Soup uses `dlopen` and
    `ns_initparse`). Missing `resolv` produces an `ns_initparse` link error.
  - macOS: `resolv`.
  - Windows: `ws2_32`, `bcrypt`, `dnsapi`.
- `Threads::Threads` through `pluto_soup`.
- `-Wno-multichar` on GNU/Clang (Pluto uses multichar literals intentionally).

Override points (cache variables):

```
-DPLUTO_GIT_REPOSITORY=...   # default https://github.com/PlutoLang/Pluto.git
-DPLUTO_GIT_TAG=...          # default 0.12.2
-DPLUTO_SOURCE_DIR=...       # build a local checkout, skips the download
```

### CMake wiring

`engine/CMakeLists.txt`:
- `add_subdirectory(libs/pluto)` (replaces the old `libs/lua`).
- `pluto pluto_soup` added to the external-warning loop.
- `pluto` linked into `doriax` as `PUBLIC`, so the editor and standalone builds
  inherit the headers and the static library.
- The old `include_directories(SYSTEM libs/lua)` is gone; include paths come
  from the `pluto` target's usage requirements.

Root `CMakeLists.txt`:
- The old `include_directories(SYSTEM ${DORIAX_ROOT}/libs/lua)` is gone for the
  same reason.

`file(GLOB ... CONFIGURE_DEPENDS)` is used for Pluto's sources because upstream
has no file list to import. This means **new upstream `.cpp` files are picked
up automatically**, and CMake re-runs its configure step on most builds.
Switching to an explicit list (or vendoring) is tracked in
`AGENTS/Plans/extension.md` as technical debt.

## Header integration and the macro shim

Pluto's public `luaconf.h` unconditionally defines 60 generic ANSI colour macros:
`ESC`, `BLK`, `RED`, `GRN`, `YEL`, `BLU`, `MAG`, `CYN`, `WHT`, their bold /
underline / background / high-intensity variants, plus `RESET`, `CRESET`,
`COLOR_RESET`. They are used only inside Pluto's implementation.

Including Pluto's `lua.hpp` therefore injects a macro named `RED` into every
translation unit, which collides with real engine identifiers such as
`ColorFormat::RED` in `engine/core/render/Render.h`. This is not theoretical: the
first full build failed on exactly that enum.

Fix: **`engine/core/script/PlutoLua.h`** includes `lua.hpp` and immediately
`#undef`s all 60 macros. All engine/editor translation units include this shim
instead of `lua.hpp`.

Consequences and rules:

- Never include `lua.hpp`, `lua.h`, `lauxlib.h`, or `lualib.h` directly in
  `engine/` or `editor/`.
- The `#undef` list is pinned to Pluto 0.12.2. Re-check it on any version bump.
- The clean fix is upstream (guard the macros behind a `PLUTO_NO_COLOR_MACROS`
  style define). Until then the shim is required. A CI grep for direct
  `lua.hpp` includes would prevent regressions.
- Pluto's `lua.h` uses `LUA_API extern "C"` under C++, so the C ABI stays
  compatible even though Pluto is compiled as C++.
- `LuaBridge.h` requires `LUA_VERSION_NUM` to be defined before it is included;
  `PlutoLua.h` must come first (this was already the include order).

## Runtime init

`LuaBinding::registerClasses` (`engine/core/script/LuaBinding.cpp`) is the only
place a Lua state is created (`createLuaState` → `luaL_newstate`). It opens:

```cpp
luaL_openselectedlibs(L, ~0, 0);
```

`luaL_openselectedlibs` is Pluto's replacement for `luaL_openlibs`; the bitmask
selects libraries. `~0` opens **everything**:

- Stock Lua: base, package, coroutine, debug, io, math, os, string, table, utf8.
- Pluto extensions: crypto, json, base32, base64, **assert**, vector3, url,
  star, cat, http, scheduler, bigint, xml, regex, ffi, canvas, buffer, socket.

Pluto's own default (`luaL_openlibs`, i.e.
`luaL_openselectedlibs(L, PLUTO_DEFAULTLOADLIBS, ~0)`) loads only the stock
libraries and merely *preloads* the extensions for `require()`. Our `~0` choice
is a deliberate departure: extensions are available as globals.

Behavioral consequences to be aware of:

- **`assert` is replaced.** Pluto's `assert` library is a callable table that
  raises an `AssertionError` object instead of the base C function that raises a
  string. `pcall(assert, false)` now yields a table-like error, not a string.
  The engine's `getLuaStackErrorString` handles non-strings (via `__tostring` /
  type name), but user scripts that string-match errors will behave differently.
- `ffi`, `socket`, `http`, `io`, `os`, `load`, and `debug` are exposed to
  scripts. Fine for trusted local projects; a sandbox concern for shipped games.
- The `exception` class and `instanceof()` global are installed by Pluto's
  startup chunk, which runs inside `luaL_openselectedlibs`.

To restore strict Lua 5.4 behavior while still shipping Pluto, replace the line
with `luaL_openselectedlibs(L, PLUTO_DEFAULTLOADLIBS, ~0);`. If you do, update
this document and the "Hard rules" in `AGENTS.md`.

## Editor integration

`editor/window/widget/CustomTextEditor.cpp`:

- `initializeLanguage()` `case SyntaxLanguage::Lua:` now contains the full Pluto
  reserved-word set plus the legacy `pluto_*` compatibility spellings, and adds
  `exception` to the type set and the Pluto library names to the builtin set.
  `.pluto` files map to this same `SyntaxLanguage::Lua`, and the label reads
  "Pluto".
- `initializeSuggestions()` `language == SyntaxLanguage::Lua` adds class / switch
  / enum / try snippets.

Because the lexer classifies words through `languageDef.keywords` and the
autocomplete reads the same set, this one edit drives both. There is no second
token list. A headless check cross-checks that list against Pluto's own
`luaX_tokens` (`tests/pluto/keyword_check.cpp`, built with
`-DDORIAX_BUILD_TESTS=ON`), so a pin move that adds a keyword fails the build.

Remaining editor gaps:

- The keyword list is still hand-maintained; the test above only catches drift,
  it does not generate the list (the single-source-of-truth stretch goal, W7.2).
- `editor/util/ScriptEvents.cpp` now counts `class`, `try` and `begin` as block
  openers alongside Lua's. `switch ... do` balances through `do` and `catch`
  does not change depth.

## `.pluto`, resolution order, and bytecode

- `.pluto` and `.lua` are both first-class; `Util::isLuaFile` accepts both.
  `require()` and the script entry point resolve in a fixed order (D2):
  `lua://<name>.pluto`, `lua://<name>.lua`, `lua://<name>.luac`, then the same
  three under `lua/`. When a `.pluto` and a `.lua` share a base name the first
  wins and the engine logs one warning naming both.
- `project.yaml` carries `scriptExtension` (`pluto` or `lua`) for newly created
  scripts and `scriptCompilation` (`source` or `bytecode`) for exports. A
  `project.yaml` written before these keys existed loads as `lua` + `source`, so
  existing projects keep creating `.lua`.
- **Bytecode** is produced in-process by
  `editor/util/ScriptCompiler.{h,cpp}`: a private scratch `lua_State` opened
  with the same `luaL_openselectedlibs(L, ~0, 0)`, `luaL_loadbufferx(..., "t")`,
  then `lua_dump(..., strip)`. Release exports strip debug info, Debug keeps it.
  A bytecode export replaces each `.pluto`/`.lua` in the exported `lua/` tree
  with `<base>.luac` and removes the text; the scene keeps the authored path and
  `LuaBinding` falls back to the compiled sibling.
- **`plutoc`** (upstream `src/luac.cpp`) is built on desktop platforms and links
  the identical `pluto` library, so its `.luac` is interchangeable with the
  editor's. Use it for CI or external tooling: `plutoc -o out.luac in.pluto`.
  The CLI export also takes `--compile-scripts`.
- Bytecode is locked to the producing Pluto build. `luaL_loadbufferx` uses mode
  `"b"` for `.luac` and `"t"` for text, so a mismatch or a misnamed file fails
  with a load error instead of being reinterpreted. The revision
  (`PLUTO_VERSION`, currently `Pluto 0.12.2`) is recorded in the exported
  `AGENTS.md`.

## Caveats and known limitations

Ordered roughly by how likely they are to matter.

1. **Reserved words break existing scripts.** See
   [`pluto-migration.md`](pluto-migration.md) for the list and the fixes.
   `local new = ...`, `{ class = ... }`, etc. stop parsing. No auto-fixer exists
   (deliberately); the editor reports the failure with a line number on save.
2. **Bytecode is tied to one Pluto build.** Shipping `.luac` from a different
   revision is rejected at load. That is the point, but it means upgrading the
   pin invalidates every precompiled artifact. The export records the revision
   to make the mismatch diagnosable.
3. **Compile diagnostics are save-time only.** The editor checks scripts on save
   and on export; it does not run a background linter, and there is no
   squiggle/marker overlay — the message goes to the Output window's Scripts
   channel. `editor/util/ProjectUtils.cpp` still parses `properties` in the live
   runtime state (D7 deferred).
4. **No sandbox.** All libraries are open (see Runtime init).
5. **`assert` semantics changed** (see Runtime init).
6. **Only Linux is verified.** The Windows/macOS link libraries come from
   Pluto's Makefile but have not been built locally. Android and Emscripten get
   no `LUA_USE_*` define and Soup's networking/threading has not been tested
   there.
7. **First configure needs network** (`FetchContent`), and the pin is a movable
   tag rather than a SHA. Offline builds need `-DPLUTO_SOURCE_DIR`.
8. **The macro shim is brittle** (60 hand-listed `#undef`s tied to 0.12.2).
9. **Source lists are globs**, so upstream changes alter our build silently.
10. **CI runs two headless checks, not a functional suite.** `pluto-runtime`
    (class/switch/enum/json + a bytecode round trip) and `pluto-editor-keywords`
    run on the Ubuntu job. There is no round-trip test through the exporter yet.
11. **No debugger or profiler** for scripts.
12. **`try`/`catch` is deprecated upstream** in 0.12.x (lowered to `pcall`, emits
    a warning). Prefer `pcall`/`xpcall` in new code and docs.

## How to re-verify

**Build** (Linux, full application):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target doriax-editor -j"$(nproc)"
```

Offline or pinned to a local checkout:

```bash
git clone --branch 0.12.2 --depth 1 https://github.com/PlutoLang/Pluto.git /tmp/Pluto
cmake -S . -B build -G Ninja -DPLUTO_SOURCE_DIR=/tmp/Pluto
```

**Headless checks in-tree** (no GUI). This is what CI runs:

```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DDORIAX_BUILD_TESTS=ON
cmake --build build-tests --target doriax-pluto-check doriax-keyword-check plutoc -j"$(nproc)"
ctest --test-dir build-tests --output-on-failure
# expected: pluto-runtime Passed, pluto-editor-keywords Passed
```

`doriax-pluto-check` is `tests/pluto/check.cpp`: it exercises `class`,
`switch`, `enum`, `json`, and a `lua_dump`/reload bytecode round trip against
the same `pluto` library the engine links. `doriax-keyword-check` compares the
editor's keyword list with Pluto's `luaX_tokens`. Neither needs a window or the
generated headers.

The standalone scratch-directory variant below still works if you want to test
Pluto without this repo's CMake:

**Headless runtime smoke test** (no GUI). In a scratch directory, with the
absolute path to this repo:

```bash
mkdir -p /tmp/pluto-check && cd /tmp/pluto-check
cat > CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(pluto_check LANGUAGES CXX)
add_subdirectory(/ABSOLUTE/PATH/doriax-pluto/engine/libs/pluto pluto-build)
add_executable(check check.cpp)
target_link_libraries(check PRIVATE pluto)
EOF
cat > check.cpp <<'EOF'
#include "lua.hpp"
#include <cstdio>
int main() {
    lua_State* L = luaL_newstate();
    luaL_openselectedlibs(L, ~0, 0);       // must match LuaBinding.cpp
    const char* s = R"(
        class Animal function speak() return "hi" end end
        class Dog extends Animal function speak() return "woof" end end
        local a = new Dog()
        switch 2 do case 1: error("x") break case 2: break default: error("y") end
        enum Color begin RED, GREEN end
        assert(a:speak() == "woof" and RED == 1)
        print("PLUTO_OK", json.encode({answer = 42}))
    )";
    if (luaL_dostring(L, s) != LUA_OK) { std::printf("FAIL: %s\n", lua_tostring(L, -1)); return 1; }
    lua_close(L);
    return 0;
}
EOF
cmake -S . -B build -G Ninja && cmake --build build && ./build/check
# expected: PLUTO_OK {"answer":42}
```

**Editor**: launch `build/doriax-editor`, open a `.pluto` file, and confirm
`class`, `switch`, `enum`, `try`, `parent`, `export` highlight and autocomplete,
and that indentation inside `class`/`enum`/`try` matches `end`. Save a file with
a deliberate syntax error and confirm it appears on the Output window's
**Scripts** channel with a line number.

**Cross-platform**: push a branch and let `.github/workflows/cmake.yml` build the
matrix. The Ubuntu job also runs the two headless checks and `plutoc`; Windows
and macOS build `plutoc` and the checks but do not run them.

## Key file reference

| Concern | File |
| --- | --- |
| Pluto fetch, targets, links | `engine/libs/pluto/CMakeLists.txt` |
| Macro shim | `engine/core/script/PlutoLua.h` |
| State creation and library opening | `engine/core/script/LuaBinding.cpp` |
| Module/`lua://` loader | `LuaBinding::moduleLoader` |
| Runtime script instantiation | `LuaBinding::initializeLuaScripts` |
| Editor property extraction from scripts | `editor/util/ProjectUtils.cpp` (`loadLuaScriptProperties*`) |
| Editor keywords / snippets | `editor/window/widget/CustomTextEditor.cpp` |
| Language detection | `editor/window/CodeEditor.cpp` (`languageForPath`) |
| Extension predicates | `editor/util/Util.h` (`isLuaFile`, `isScriptFile`) |
| Export filters / copy | `editor/Exporter.cpp` (`isLuaExportFile`, `isLuaSourceFile`, `copyLua`, `compileLuaBytecode`) |
| In-process bytecode compiler | `editor/util/ScriptCompiler.{h,cpp}` |
| `plutoc` CLI target | `engine/libs/pluto/CMakeLists.txt` |
| Headless checks | `tests/pluto/` (`-DDORIAX_BUILD_TESTS=ON`) |
| Extension setting / serialization | `editor/Project.h`, `editor/Stream.cpp` (`scriptExtension`, `scriptCompilation`) |
| Reserved-word migration guide | `AGENTS/Docs/pluto-migration.md` |
| Script type / path | `engine/core/component/ScriptComponent.h` |
