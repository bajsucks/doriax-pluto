# AGENTS.md

Operating notes for anyone (human or agent) working in this repository.

This is a fork of **Doriax** — a native C++ 2D/3D game engine with a visual
editor — in which the scripting runtime has been swapped from vanilla
**Lua 5.4.7** to **Pluto** (a Lua 5.4 superset). Read
[`AGENTS/Docs/pluto.md`](AGENTS/Docs/pluto.md) before touching anything that
parses or runs scripts.

## Index

- [`AGENTS/Docs/pluto.md`](AGENTS/Docs/pluto.md) — how Pluto is wired in, `.pluto`
  support, bytecode, and its caveats.
- [`AGENTS/Docs/pluto-migration.md`](AGENTS/Docs/pluto-migration.md) — the
  reserved-word break and how to fix it.
- [`AGENTS/Plans/`](AGENTS/Plans/) — in-flight work items. Start with
  [`extension.md`](AGENTS/Plans/extension.md) (`.pluto` support + `plutoc`).

When you finish a piece of work that changes architecture or leaves known
limitations, add or update a page under `AGENTS/Docs/`, and record unfinished
work as a plan under `AGENTS/Plans/` so the next agent can pick it up cold.

## Hard rules

These are easy to break and expensive to notice:

1. **Never `#include "lua.hpp"` (or `lua.h`/`lauxlib.h`/`lualib.h`) directly**
   in `engine/` or `editor/`. Include `"PlutoLua.h"` instead. Pluto's public
   `luaconf.h` leaks generic ANSI colour macros (`RED`, `RESET`, …) that collide
   with engine enums; `PlutoLua.h` includes Pluto and `#undef`s them. A direct
   include silently breaks unrelated files like `ColorFormat::RED`.
2. **Do not bump Pluto past the 0.12.x line without a plan.** 0.12.2 is the last
   release based on Lua 5.4 (5.4.8). Pluto 0.13 is Lua 5.5, dropped the
   `try`/`catch` and `global` keywords, and is not supported by the pinned
   LuaBridge3. See `AGENTS/Docs/pluto.md` for details.
3. **`engine/libs/lua` no longer exists.** The vanilla Lua sources were deleted
   as part of the swap. The runtime target is `pluto` (plus its support library
   `pluto_soup`).
4. **Keep `pluto` and `pluto_soup` in both places** in `engine/CMakeLists.txt`:
   the external-warning-suppression loop **and** the `target_link_libraries(doriax ...)`
   list. Missing the warning loop drowns the build in third-party warnings;
   missing the link list breaks the editor and exported builds.
5. **Reserved words.** Pluto makes `class`, `switch`, `case`, `default`, `enum`,
   `new`, `continue`, `parent`, `export`, `try`, `catch`, `global`, `as`,
   `begin`, `extends`, `instanceof`, and `pluto_use` keywords. Code, scripts, or
   generated identifiers using those names will fail to parse. Scripts can rename
   the identifier or disable the keyword with `pluto_use`; see
   [`AGENTS/Docs/pluto-migration.md`](AGENTS/Docs/pluto-migration.md).
6. **`luaL_openselectedlibs(L, ~0, 0)` is deliberate but opinionated.** It opens
   every Pluto library globally, including `ffi`, `socket`, `http`, and Pluto's
   replacement `assert`. If you change it, read the "Runtime init" section of
   `AGENTS/Docs/pluto.md` and update that doc. The in-process bytecode compiler
   (`editor/util/ScriptCompiler.cpp`) opens the same set, so keep them in step.
7. **`.pluto` and `.lua` are both first-class.** Never assume one extension.
   Scene script paths keep whatever the author wrote; resolution order lives in
   `LuaBinding::moduleLoader` (`.pluto` before `.lua`, assets root before `lua/`),
   and bytecode exports rely on the `.luac` fallback. `.luac` is an export
   artifact, never an authoring file.

## Build

The project has two CMake entry points:

- **Root `CMakeLists.txt`** — builds `doriax-editor` (the full application).
  It adds `engine/` via `add_subdirectory(${DORIAX_ROOT})`.
- **`engine/CMakeLists.txt`** — also usable as a top-level project; this is the
  path the editor's exporter emits for generated/standalone projects. Read it
  from the top to see the standalone branch.

Desktop (Linux, verified):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target doriax-editor -j"$(nproc)"
```

- **First configure needs network**: Pluto is pulled with `FetchContent`
  from GitHub (pinned tag). For offline or reproducible builds pass a checkout:
  `-DPLUTO_SOURCE_DIR=/path/to/Pluto` (must be the pinned revision) or
  `-DPLUTO_GIT_TAG=<sha>`.
- **`plutoc`** is an extra desktop target (upstream `luac.cpp`) that compiles
  `.pluto`/`.lua` to `.luac`. It is skipped on Android/iOS/Emscripten. Build it
  with `cmake --build build --target plutoc`.
- **`-DDORIAX_BUILD_TESTS=ON`** adds the headless script checks:
  `doriax-pluto-check` (Pluto syntax, `class`/`switch`/`enum`/`json`, and a
  bytecode round trip) and `doriax-keyword-check` (editor keywords vs Pluto's
  lexer). Run them with `ctest --test-dir build --output-on-failure`. Off by
  default; CI turns them on.
- C++ standard is 17 by default; 20 and 23 are allowed
  (`DORIAX_CXX_STANDARD`, see `engine/CMakeLists.txt:3`). Pluto itself is built
  as C++17 regardless.
- `DORIAX_SHARED` is `ON` for the editor and `OFF` for standalone builds.
- Third-party code lives in `engine/libs/*` and is linked into `doriax`. Do not
  add project warnings to it; it is built with the external warning flag (`-w`/`/w`).
- Generated headers land in the build tree: `shaders.h`
  (`generate_shaders.cmake`) and `engine_api_suggestions.h`
  (`generate_api_suggestions.py`, which parses `engine/core/script/binding/*.cpp`).

## Repository map

| Area | Where |
| --- | --- |
| Engine runtime / ECS / objects | `engine/core/` |
| Scripting C API + bindings | `engine/core/script/` (`LuaBinding.cpp`, `binding/*.cpp`) |
| Pluto header shim | `engine/core/script/PlutoLua.h` |
| Pluto CMake targets (`pluto`, `pluto_soup`, `plutoc`) | `engine/libs/pluto/CMakeLists.txt` |
| Editor application shell (ImGui) | `editor/` |
| Embedded code editor | `editor/window/widget/CustomTextEditor.cpp`, `editor/window/CodeEditor.cpp` |
| Script/project file handling | `editor/util/Util.h`, `editor/Project.cpp`, `editor/util/ProjectUtils.cpp` |
| In-process script compiler (bytecode, diagnostics) | `editor/util/ScriptCompiler.{h,cpp}` |
| Export pipeline | `editor/Exporter.cpp`, `editor/Factory.cpp`, `editor/Generator.cpp` |
| AI scripting actions | `editor/ai/EditorActionExecutor.cpp`, `editor/ai/EditorActionRegistry.cpp` |
| Headless script checks | `tests/pluto/` (built with `DORIAX_BUILD_TESTS`) |
| Engine sample project / script template | `engine/project/` |

## Things that will bite you

- **The engine has no functional test suite, but the script runtime has two
  headless checks.** `doriax-pluto-check` and `doriax-keyword-check` cover Pluto
  syntax, the bytecode round trip, and editor keyword drift; they do not cover
  the editor, exporter, or scene loading. Verification of anything else still
  means: configure, build `doriax-editor`, and run a script. "It compiles" is
  not "it works".
- **CI runs those two checks only on the Ubuntu job.** The matrix builds Windows
  (MSVC + MinGW), Ubuntu GCC, and macOS Clang, and now also builds `plutoc` and
  the checks everywhere. Only Linux has been exercised locally; the
  Windows/macOS/Android/Emscripten link paths are unverified.
- **The editor's language data is hand-maintained.** Keywords, types, snippet
  lists, and the Lua/Pluto block-depth parser live in `CustomTextEditor.cpp` and
  `editor/util/ScriptEvents.cpp`. `doriax-keyword-check` fails the build when the
  keyword list drifts from Pluto's lexer, but the list is still edited by hand;
  the single-source-of-truth generation remains a stretch goal in
  `AGENTS/Plans/extension.md`.
- **Script extensions are accepted in pairs, not hard-coded to `.lua`.** Use
  `Util::isLuaFile` / `Util::isScriptFile` (`editor/util/Util.h`) rather than
  comparing an extension string. `Project::getScriptFileExtension()` is what new
  scripts use; `.luac` is deliberately excluded from the authoring predicates.
- **`NO_LUA_INIT` / `DISABLE_LUA_BINDINGS`** compile-time switches exist
  (root `CMakeLists.txt`, `engine/CMakeLists.txt`, `LuaBinding.cpp`). Check them
  before assuming a code path always runs.
- **LuaBridge3 requires the Lua headers first.** `LuaBridge.h` errors if
  `LUA_VERSION_NUM` is not already defined, so `PlutoLua.h` must precede it.
- **Editor and exported game load scripts through the `lua://` virtual path**
  (`LuaBinding::moduleLoader`, `initializeLuaScripts`), not the OS filesystem.
- **Bytecode is version-locked.** `.luac` only loads in the Pluto build that
  produced it. The editor compiles in-process (`ScriptCompiler`); `plutoc` links
  the same library on purpose. Do not introduce a second producer.

## Conventions

- Files start with the header used across the codebase:
  `// (c) Eduardo Doria and contributors` + `// SPDX-License-Identifier: MIT`.
- Comments explain *why*, in full sentences; match the surrounding style rather
  than the terse style of vendored third-party code.
- CMake targets: `pluto` (runtime), `pluto_soup` (Pluto's support library),
  `plutoc` (desktop bytecode CLI), `doriax` (engine), `doriax-editor`
  (application), plus the opt-in `doriax-pluto-check` / `doriax-keyword-check`.
- Keep `AGENTS/Docs/*.md` factual and current; date or version anything that
  will rot (e.g. "as of Pluto 0.12.2").

## Quick sanity check

After changing anything in the scripting path:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDORIAX_BUILD_TESTS=ON
cmake --build build --target doriax-editor plutoc doriax-pluto-check doriax-keyword-check -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/doriax-editor   # open a .pluto file, confirm highlighting + autocomplete
```

`AGENTS/Docs/pluto.md` documents the checks and a standalone headless smoke test
that exercises Pluto syntax and the extension libraries without launching the
GUI.
