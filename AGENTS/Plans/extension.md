# Plan: `.pluto` support end-to-end + `plutoc` precompilation

> This plan implements item **#7** from the Pluto integration review: make
> `.pluto` a first-class script extension across the engine, editor, exporter,
> and AI surfaces, and add optional `plutoc` bytecode precompilation.
>
> Read [`../Docs/pluto.md`](../Docs/pluto.md) first. It documents the current
> integration and the caveats this plan assumes.

## Status (implementation pass)

**Shipped: M1, M2, M3 (minus the compile cache), and most of M4.** The build is
green on Linux with `plutoc`, and both headless checks pass
(`ctest -DDORIAX_BUILD_TESTS=ON`: `pluto-runtime`, `pluto-editor-keywords`).

What is **not** done, and why:

- **W5.3 (compile cache)** — deliberately skipped. An export compiles each
  script once and Play uses source, so a content-hash cache has no measured
  consumer yet. Add it when repeated exports of a large project make it hurt;
  the recommended location remains `.doriax/`.
- **W7.3 (export round-trip test)** — needs a project fixture and the exporter
  driving the real runtime; there is no headless harness for that yet. This is
  the highest-value missing test because it is the only one that catches an
  exporter/runtime extension split.
- **W7.4 (D2 ambiguity regression test)** — the ordering and warning are in
  `LuaBinding::moduleLoader`, but exercising it needs the engine runtime plus
  the `lua://` virtual filesystem.
- **W8.2 (D9 scanner)** — the migration guide exists and the save-time
  diagnostic links it, but there is no automated project/CLI scan for reserved
  words used as identifiers. Auto-fix stays out of scope by decision.

Verified this pass: `.pluto` and `.lua` parse and run headless; `lua_dump`
bytecode round-trips through `luaL_loadbufferx(..., "b")`; a text chunk misnamed
`.luac` is rejected in text mode (and vice versa); the editor's
`ScriptCompiler` compiled a `.pluto` class and the engine-style loader ran it;
the editor keyword list matches Pluto's `luaX_tokens`; `plutoc` compiles a
`.pluto` file; the editor and CLI build. **Not verified**: renaming/copying
through the GUI, editor Play, a full export, bytecode export end-to-end, and
every non-Linux target.

## Objective

A project using the fork should be able to:

1. Author scripts as `*.pluto` using Pluto syntax (`class`, `switch`, `enum`,
   `try`, etc.), side by side with existing `*.lua` files.
2. Create, open, highlight, rename, attach, and run `.pluto` scripts entirely
   through the editor.
3. Export a build that ships either `.pluto` **source** or precompiled **Pluto
   bytecode** (`.luac`), with the bytecode produced by the exact pinned Pluto
   revision and a clear error if the revision ever mismatches.
4. Get syntax errors surfaced in the editor instead of only in the engine log.
5. Keep existing `.lua` projects working unchanged.

## Non-goals

- A sandbox/permission model for `ffi`/`socket`/`http` (separate effort; see
  `../Docs/pluto.md` caveats).
- Migrating to Pluto 0.13 / Lua 5.5.
- A script debugger or profiler.
- Making the editor's keyword list authoritative (a follow-up is noted, but the
  single-source-of-truth work is scoped as an optional stretch in W7).
- Changing the `.lua` on-disk format or the `lua://` virtual path scheme.

## Current state (what exists today)

All script handling keys off the string `".lua"` in these places:

**Engine runtime**

| Location | Behavior |
| --- | --- |
| `engine/core/script/LuaBinding.cpp` `moduleLoader` | `require("x")` tries `lua://x.lua`, then `lua://lua/x.lua`. |
| `engine/core/script/LuaBinding.cpp` `init()` | Loads `lua://main.lua`, then `lua://lua/main.lua`. |
| `engine/core/script/LuaBinding.cpp` `initializeLuaScripts()` | Loads `lua://<entry.path>` with `luaL_loadbuffer`, then runs it and expects a module table. Path comes from `ScriptEntry::path`; extension-agnostic as long as the file exists. |
| `engine/core/component/ScriptComponent.h` | `ScriptType { CPP, LUA }`, `path` documented as `.cpp or .lua`. |

**Editor**

| Location | Behavior |
| --- | --- |
| `editor/util/Util.h` | `isScriptFile()` and `isLuaFile()` extension sets. |
| `editor/window/CodeEditor.cpp` | `languageForPath()` maps `.lua` → `SyntaxLanguage::Lua`; `collectProjectSources()` splits Lua vs C++ by `isLuaFile`; `showEvents` uses `isLuaFile`. |
| `editor/window/dialog/ScriptCreateDialog.cpp` | `makeLuaPath()` appends `.lua`; `writeFiles()` emits a Lua template; `selectExistingFile()` validates with `isLuaFile`. |
| `editor/window/ResourcesWindow.cpp` | New-item flow appends `.lua`; file type detection. |
| `editor/Project.cpp` | `.gitattributes` generator emits `*.lua text eol=lf`; `normalizeToLuaRelative`/`resolveLuaPath`; `holdsOnlyAssets` uses `isScriptFile`; `updateScriptProperties` dispatches Lua entries to `ProjectUtils`. |
| `editor/util/ProjectUtils.cpp` | `loadLuaScriptProperties()` / `loadLuaScriptPropertiesFromString()` parse the script's `properties` table using the shared Pluto state. |
| `editor/Exporter.cpp` | `isLuaExportFile()` (allowlist for the lua tree) already includes `.luac`; `isLuaSourceFile()` returns `ext == ".lua" || ext == ".luac"`; `copyLua()` walks the lua root. |
| `editor/ai/EditorActionExecutor.cpp` | `scriptTypeExtension()`; extension validation in `updateScriptEntry`/`updateScriptFile`; extension lists for allowed edits. |
| `editor/ai/EditorActionRegistry.cpp` | Schema descriptions and validation messages list `.lua`. |
| `editor/Generator.cpp` | Generated `AGENTS.md` text says Lua scripts are `.lua`. |
| `editor/util/ScriptEvents.cpp` | `luaDocument()` block-depth parser knows `function`/`if`/`do`/`repeat`/`end`/`until` only. |

**Extension-agnostic already (no change needed)**

- `editor/Factory.cpp` serializes `ScriptEntry::path` as an opaque string.
- `engine/core/script/LuaBinding.cpp` `initializeLuaScripts()` uses the path
  verbatim; it does not inspect the extension.
- `luaL_loadbuffer` auto-detects text vs binary chunks via Pluto's
  `LUA_SIGNATURE` (`"\x1bLua"`).

**Toolchain facts (verified against Pluto 0.12.2)**

- `plutoc` is upstream's `src/luac.cpp`. It links against the same `pluto`
  library and needs no readline (only `lua.cpp`, the REPL, includes readline).
- The bytecode writer is `lua_dump(L, writer, data, strip)` (`lua.h`), and the
  loaders are `luaL_loadbufferx(L, buf, len, name, mode)` /
  `luaL_loadfilex(L, path, mode)`.
- Bytecode is **version-locked** to the producing Pluto build; mismatches fail
  at load with a version error.

## Design decisions

Each decision has a recommendation. Deviating is fine, but update this file.

### D1 — Authoring extension

Accept `.pluto` and `.lua` everywhere, with `.pluto` as the preferred extension
for new scripts. Do **not** rename or auto-convert existing `.lua` files.
`.lua` files are valid Pluto input (it is a superset) and must keep working.

**Recommendation:** accept both; treat `.pluto` as canonical for new content.

### D2 — Resolution order and ambiguity

When `require("enemy")` runs, or when two files share a base name
(`enemy.pluto` and `enemy.lua`), resolution must be deterministic.

**Recommendation:** search `.pluto` before `.lua`, then the `lua/` subdir
variants in the same order:

```
lua://<name>.pluto
lua://<name>.lua
lua://lua/<name>.pluto
lua://lua/<name>.lua
```

If both `<name>.pluto` and `<name>.lua` exist, load the first match and emit a
one-time `Log::warning` naming both files so a stale duplicate is visible.
Mirror this order in the editor's "two files at the same path" export warnings.

### D3 — What ships: source vs bytecode

**Recommendation:** introduce a project setting, e.g. `scriptCompilation` in
`project.yaml`:

- `source` (default): ship `.pluto`/`.lua` text. Editor Play and local dev use
  this, so hot reload and readable stack traces are preserved.
- `bytecode`: at export time, compile every shipped script and ship `.luac`.
  Keep the original text out of the export (or retain it in a non-shipped
  folder) to avoid leaking source if that matters to the user.

Do not change the runtime loader signature for this: `luaL_loadbuffer` already
handles both. For `.luac` files, prefer `luaL_loadbufferx(..., "b")` so a text
file misnamed as bytecode fails clearly and a binary blob is never treated as
text (and vice versa for `.pluto` with `"t"`).

### D4 — How bytecode is produced

Two options:

1. **In-process dump (recommended for the editor/export path).** Create a
   throwaway `lua_State` (`luaL_newstate` + `luaL_openselectedlibs` to match the
   runtime), `luaL_loadbufferx` the source, then `lua_dump` with
   `strip = 1` into a memory buffer. Advantages: exactly the runtime's Pluto
   revision, no subprocess, no extra binary to locate, works on every platform.
2. **`plutoc` subprocess.** Build `plutoc` as a CMake target and shell out.
   Advantages: inspectable artifacts, usable by external tooling/CI, and a
   natural home for static checks (`-l`, `-p`).

**Recommendation:** implement (1) for editor/export, and (2) as an optional
`plutoc` build target for CLI/CI and users who want it. Both must be version
stamped (see W5).

### D5 — Editor language vs a new syntax enum

`.pluto` is Lua-plus-keywords; the existing `SyntaxLanguage::Lua` branch already
contains the Pluto data.

**Recommendation:** map `.pluto` → `SyntaxLanguage::Lua`. Do not add a new enum
value unless a Pluto-only grammar feature (e.g. `$"..."` interpolation) needs
distinct highlighting. Optionally change the display label from "Lua" to "Pluto"
(one place: `CustomTextEditor.cpp` language-name switch).

### D6 — Default extension for new scripts

**Recommendation (updated after product feedback):** add a project setting
(`scriptExtension`, values `pluto` / `lua`) and make `.pluto` the default
everywhere a new script is created, including in a `project.yaml` written before
the key existed. Only an explicit `lua` in that file (or Project Settings >
Directories) creates `.lua`. Existing `.lua` files are never renamed or
converted. `ScriptCreateDialog`, `ResourcesWindow`, and the AI `create_script`
action must all honor it, or users will get inconsistent files.

### D7 — Property parsing state

`editor/util/ProjectUtils.cpp` currently parses script `properties` tables in the
**shared runtime state** (`LuaBinding::getLuaState()`), via `luaL_loadfile` /
`luaL_loadstring`. That state is the live editor scripting VM; running arbitrary
(possibly mid-edit) script content in it is already a little risky.

**Recommendation:** for the `.pluto` work, move property extraction onto a
dedicated scratch `lua_State` with a protected `pcall`. This also makes
bytecode-vs-source handling explicit and avoids polluting `package.loaded` with
half-edited modules. Treat as a prerequisite only if you touch this code anyway;
otherwise file it as a follow-up.

### D8 — Diagnostics

**Recommendation:** add an editor-visible compile step. On save (and on export),
load the file with `luaL_loadbufferx` in the scratch state and surface the error
message, file, and line. The editor's code editor should show a marker and the
Output panel should get a "Scripts" channel. This is the highest-value
user-facing improvement after basic `.pluto` support.

### D9 — Reserved-word migration

`.pluto` users coming from `.lua` may have identifiers that are now keywords.

**Recommendation:** ship a project-level scan (and a CLI pass) that reports
`class`, `switch`, `case`, `default`, `enum`, `new`, `continue`, `parent`,
`export`, `try`, `catch`, `global`, `as`, `begin`, `extends`, `instanceof`,
`pluto_use` used as identifiers, with a link to the migration guide. Auto-fix is
risky; report first.

### D10 — Single source of truth for keywords (stretch)

The editor keyword list and `luaDocument()` block parser duplicate Pluto's
lexer.

**Recommendation:** generate the editor keyword set from a checked-in list that
a test cross-checks against Pluto's `llex.cpp` (see W7). Runtime generation from
Pluto is not worth the coupling.

## Workstreams

Task IDs are stable; reference them in commits and in the plan checkboxes.

### W1 — Engine runtime `.pluto` loading

Files: `engine/core/script/LuaBinding.cpp`,
`engine/core/component/ScriptComponent.h`, `engine/project/lua/`.

- [x] **W1.1** `moduleLoader`: try `.pluto` then `.lua`, for both the assets root
      and the `lua/` subdirectory, in the D2 order. Keep the `"no file in assets
      directory"` fallback message.
- [x] **W1.2** `init()`: look for `lua://main.pluto` before `lua://main.lua`, and
      the `lua/` variants likewise.
- [x] **W1.3** `initializeLuaScripts()`: no extension logic expected, but
      confirm a `.pluto` path loads and that a `.luac` path loads; pass the
      path (not `className`) as the chunk name so stack traces stay useful.
- [x] **W1.4** Update the `ScriptEntry::path` comment in `ScriptComponent.h` to
      say `.cpp`, `.lua`, or `.pluto`.
- [x] **W1.5** Add `engine/project/lua/main.pluto`? **Do not** replace
      `main.lua` in the template yet; instead let W3 create the right one, or add
      both and rely on the D2 order. Decide before editing the sample project.

**Acceptance:** a scene with a `.pluto` script using `class` runs in editor Play
and in a standalone/exported build; an existing `.lua` project is unaffected.

### W2 — Editor file types and language

Files: `editor/util/Util.h`, `editor/window/CodeEditor.cpp`,
`editor/window/widget/CustomTextEditor.cpp`, `editor/util/ScriptEvents.cpp`,
`editor/window/Properties.cpp`, `editor/window/AiChatWindow.cpp`,
`editor/command/type/RenameFileCmd.cpp`, `editor/command/type/CopyFileCmd.cpp`.

- [x] **W2.1** `Util::isLuaFile`: accept `.pluto` (and `.luac`? see W4.4 —
      probably yes for consistency). `Util::isScriptFile`: accept `.pluto`.
- [x] **W2.2** `CodeEditor::languageForPath`: map `.pluto` → `SyntaxLanguage::Lua`.
- [x] **W2.3** `CustomTextEditor`: optionally relabel "Lua" → "Pluto"; keep the
      keyword data as-is (it already includes Pluto keywords).
- [x] **W2.4** `ScriptEvents.cpp` `luaDocument()`: add the Pluto block openers
      `class` and `try`, and `begin` (for `enum ... begin`). `switch ... do`
      already balances via `do`. Verify `catch` does not change depth.
- [x] **W2.5** Audit `Util::isScriptFile` / `isLuaFile` consumers for
      extension-driven behavior that should now include `.pluto`:
      `Properties.cpp:8948`, `AiChatWindow.cpp:1811`,
      `RenameFileCmd.cpp:58,99`, `CopyFileCmd.cpp:79,149`.
- [x] **W2.6** Confirm rename/copy path remapping (`.lua` ↔ `.pluto`) still
      rewrites `ScriptEntry::path` correctly.

**Acceptance:** creating, opening, editing, renaming, and copying a `.pluto` file
in the editor behaves like `.lua`; highlighting and autocomplete work; indentation
inside `class`/`enum`/`try` is correct.

### W3 — Creation, resources, and project metadata

Files: `editor/window/dialog/ScriptCreateDialog.cpp`,
`editor/window/ResourcesWindow.cpp`, `editor/Project.cpp`,
`editor/Project.h`, `editor/cli/CommandLine.cpp`.

- [x] **W3.1** Add the D6 project setting (`scriptExtension`). Surface it in
      Project Settings > Directories next to the Lua dir.
- [x] **W3.2** `ScriptCreateDialog::makeLuaPath()` and `writeFiles()` honor the
      setting; the template body should stay valid in both dialects
      (`local M = {} ... return M`) unless the user picked Pluto, in which case a
      Pluto-flavored template (e.g. a `class`) is a nice touch.
- [x] **W3.3** `ScriptCreateDialog::selectExistingFile()` validation accepts
      `.pluto`, and the error strings stop saying "(.lua)" only.
- [x] **W3.4** `ResourcesWindow` new-item flow appends the configured extension
      instead of hard-coded `.lua`; file-type icon/detection accepts `.pluto`.
- [x] **W3.5** `Project::versionControlAttributes` emits `*.pluto text eol=lf`
      (and `*.luac binary` if W4 ships bytecode). Existing projects regenerate
      `.gitattributes` on save — verify that path.
- [x] **W3.6** Check `Project::holdsOnlyAssets` and any "script root" scanning
      that uses `isScriptFile` so `.pluto` under a script dir is compiled/exposed
      as expected.

**Acceptance:** every new script (New Script dialog, Resources window, AI
`create_script`) is `.pluto` by default, in new and pre-existing projects alike;
a project that explicitly sets `scriptExtension: lua` keeps `.lua`; existing
scripts are never converted; `.gitattributes` matches whatever is shipped.

### W4 — Export and packaging

Files: `editor/Exporter.cpp`, `editor/Exporter.h`, `editor/cli/CommandLine.cpp`.

- [x] **W4.1** `isLuaExportFile()`: add `.pluto` to the allowlist.
- [x] **W4.2** `isLuaSourceFile()`: add `.pluto`.
- [x] **W4.3** `copyLua()` and the `copyAssets()` duplicate-path merge
      (`Exporter.cpp` ~1647-1672 and ~1723-1724) must treat `.pluto` as a Lua
      source. The Android merge failure on duplicates is the reason these checks
      exist; add `.pluto` to the same predicate rather than parallel logic.
- [x] **W4.4** Decide whether `.luac` is an authoring extension or only an export
      artifact. Recommendation: **not** an authoring extension; accept it at
      runtime/export only.
- [x] **W4.5** If D3 bytecode is enabled, insert a compile step after
      `copyLua()`/before packing: compile each shipped script with the D4
      in-process dumper, write `.luac`, and exclude the original text. Reuse the
      "removable packed tree entries" logic (`Exporter.cpp` ~1254-1281) and
      update it if it assumes `.lua`.
- [x] **W4.6** Record the Pluto revision in the exported manifest/`AGENTS.md`
      so a bytecode version mismatch is diagnosable.
- [x] **W4.7** `editor/ai/EditorActionExecutor.cpp:4474` builds an exporter
      config from `getLuaPath()`; confirm the new setting flows through there and
      through `editor/cli/CommandLine.cpp:545`.

**Acceptance:** exporting a project with `.pluto` sources produces a runnable
build; exporting with `scriptCompilation: bytecode` produces only `.luac` (plus
non-script data) and runs; the exported project has no `.pluto` text left when
bytecode is selected.

### W5 — `plutoc` and the precompile toolchain

Files: `engine/libs/pluto/CMakeLists.txt`, new
`editor/util/ScriptCompiler.{h,cpp}` (or similar), `editor/Exporter.cpp`,
root `CMakeLists.txt` (where the editor target is defined).

- [x] **W5.1** Add an executable target to
      `engine/libs/pluto/CMakeLists.txt`:

      ```cmake
      add_executable(plutoc "${PLUTO_SRC_DIR}/luac.cpp")
      target_link_libraries(plutoc PRIVATE pluto)
      set_target_properties(plutoc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
      ```

      Apply the same platform `LUA_USE_*` define as `pluto`, add `plutoc` to the
      external-warning loop in `engine/CMakeLists.txt`, and decide whether it is
      built by default or only on demand (recommend: build by default on desktop,
      skip on mobile/console where the editor does not run).
- [x] **W5.2** Implement the in-process compiler used by the editor/export:
      scratch `lua_State`, `luaL_openselectedlibs` matching the runtime,
      `luaL_loadbufferx(..., "t")`, `lua_dump(L, writer, &buf, 1)`. Return a
      structured result (ok + bytes, or error message + line).
- [ ] **W5.3** Cache compiled output keyed by a content hash + Pluto revision;
      invalidate on source change or version change. Decide the cache location
      (recommend inside the editor's internal `.doriax/` directory, never next to
      the source).
- [x] **W5.4** Version guard: stamp the Pluto revision (e.g. the
      `PLUTO_VERSION` string, `"Pluto 0.12.2"`, plus the git tag/commit from
      `PLUTO_GIT_TAG`) into cached/exported bytecode metadata and refuse to load
      a mismatched artifact with a clear message.
- [x] **W5.5** Expose "Compile scripts" as an editor action and a CLI flag
      (e.g. `--compile-scripts`) so CI can validate without a full export.
- [x] **W5.6** Document the interaction with `strip`: stripping removes local
      names and debug info, which degrades stack traces. Recommendation: ship
      stripped bytecode in Release exports and unstripped in Debug.

**Acceptance:** `plutoc -o out.luac in.pluto` builds and the engine loads
`out.luac`; the editor compiles a `.pluto` file to bytecode without spawning a
process; a bytecode file from a different Pluto revision is rejected with a
useful error.

### W6 — Diagnostics and AI surfaces

Files: `editor/ai/EditorActionExecutor.cpp`,
`editor/ai/EditorActionRegistry.cpp`, `editor/Generator.cpp`,
`editor/window/widget/CustomTextEditor.cpp` (error markers), Output panel.

- [x] **W6.1** `validateDoriaxLuaScriptContent()` is heuristic and text-based; it
      already applies to Pluto content. Make its call sites accept `.pluto` and
      consider rejecting constructs that only parse in one dialect if that is
      ever desired (probably not — both are valid).
- [x] **W6.2** `updateScriptEntry` (`new_path` validation) and
      `updateScriptFile` accept `.pluto` (and `.luac` where appropriate). Files:
      `EditorActionExecutor.cpp` ~3996-4005, ~4050-4067.
- [x] **W6.3** Update extension lists/messages in `EditorActionExecutor.cpp`
      (~1977, ~5304, ~5379) and `EditorActionRegistry.cpp` (~684, ~700, ~722,
      ~1605).
- [x] **W6.4** `Generator.cpp` (~1241): generated `AGENTS.md` should say scripts
      are `.lua` **or** `.pluto` and are loaded at runtime.
- [x] **W6.5** D8: surface compile errors on save. Add a "Scripts" output
      channel; wire the scratch compiler from W5.2.
- [x] **W6.6** If the editor gains a `scriptExtension` setting, make the AI
      prompts/`search_engine_api` examples reflect it.

**Acceptance:** the AI can create and edit `.pluto` files; a `.pluto` file with a
syntax error shows an editor-visible diagnostic with a line number.

### W7 — Tests and CI

Files: `.github/workflows/cmake.yml`, new test sources, `CMakeLists.txt`.

- [x] **W7.1** Add a headless smoke run to CI: build `plutoc` (and/or a tiny
      `pluto`-linked test) and execute a script exercising `class`, `switch`,
      `enum`, and a Pluto extension (`json`). Fail the job on a non-zero exit.
      The program in `../Docs/pluto.md` is a starting point.
- [x] **W7.2** Add a unit test that cross-checks the editor keyword list against
      Pluto's reserved words. Simplest robust form: check a small checked-in
      expected list in both places (a `.txt` parsed by the test and by the
      editor initializer), and a second assertion that the list matches
      `llex.cpp`'s `luaX_tokens` for the pinned tag (parse the fetched source at
      test time, or vendor the token list).
- [ ] **W7.3** Add a round-trip test: create a `.pluto` script → parse its
      `properties` table → export → run the exported entry point headless.
      This is the only test that catches the exporter/runtime extension split.
- [ ] **W7.4** Add a regression test for the D2 ambiguity rule (both `.pluto` and
      `.lua` present) if feasible in the runtime test harness.
- [x] **W7.5** Ensure the Windows/macOS CI jobs still build with `plutoc` in the
      tree (the executable is unnecessary on mobile; gate it).

**Acceptance:** CI fails if `.pluto` stops loading or if the editor keyword list
drifts from Pluto.

### W8 — Documentation and migration

Files: `README.md`, `AGENTS.md`, `AGENTS/Docs/pluto.md`, user docs (external repo
`doriax-site`), `engine/project/`.

- [x] **W8.1** Update `AGENTS.md` and `AGENTS/Docs/pluto.md` to reflect that
      `.pluto` is supported and how bytecode is produced.
- [ ] **W8.2** Add a migration guide for the reserved words (D9) and link it from
      the compile-error message.
- [x] **W8.3** Update the sample project (`engine/project/`) and any templates.
- [x] **W8.4** README/scripting docs should say Pluto (Lua 5.4 superset) and list
      the extension setting.

**Acceptance:** a reader who has never seen the fork can author and ship a
`.pluto` script after reading the docs.

## Milestones

| Milestone | Contents | Exit criteria |
| --- | --- | --- |
| **M1 — `.pluto` runs and edits** | W1, W2, W3 | `.pluto` scripts run in Play and highlight/complete in the editor. |
| **M2 — `.pluto` ships (source)** | W4 (source path), W6.1–W6.4 | Export of a `.pluto` project runs; AI surfaces accept `.pluto`. |
| **M3 — bytecode** | W5, W4.5–W4.7 | Editor and CLI can precompile; exported bytecode runs; version guard works. |
| **M4 — polish and guardrails** | W6.5, W7, W8 | Diagnostics, tests, docs; CI green. |

Do not start M3 before M2 is exercised in an exported build — the export/runtime
extension split is where bugs hide.

## Test matrix

| Scenario | Expected |
| --- | --- |
| Existing `.lua` project, unchanged | Builds and runs exactly as before. |
| New `.pluto` script, editor Play | Runs; hot reload works. |
| `.pluto` with `class`/`switch`/`enum` | Parses and runs. |
| `.pluto` with a syntax error | Editor shows file + line; engine logs on Play. |
| `.pluto` + `.lua` same base name | Deterministic load (`.pluto` wins) + one warning. |
| Export, `scriptCompilation: source` | `.pluto` text in the export; runs. |
| Export, `scriptCompilation: bytecode` | Only `.luac` shipped; runs. |
| Bytecode from a different Pluto revision | Rejected with a clear version error. |
| Rename `.lua` → `.pluto` | Attached script path updates; still runs. |
| Android export with `.pluto` | No duplicate-path failure in merged assets. |
| Windows/macOS build | `plutoc` + editor build. |

## Risks and open questions

1. **Bytecode version lock.** Shipping `.luac` ties the export to one exact Pluto
   build. Decide whether the exporter embeds the revision and how upgrades
   invalidate caches. (W5.4.)
2. **Reserved words.** The single biggest user-facing break. Needs the D9 scan
   and documentation.
3. **`.luac` in version control.** Users may commit bytecode. `Project.cpp`
   gitattributes should mark it binary, and docs should discourage committing it.
4. **Duplicate `.pluto`/`.lua`.** Deterministic order mitigates, but a warning is
   required or users will debug the wrong file. (W1.1, W7.4.)
5. **Android merged assets.** `Exporter::copyLua`/`copyAssets` have delicate
   duplicate handling; extending the predicate in one place but not the other
   will break Android. (W4.3.)
6. **Property parsing in the live VM.** D7 mitigation may be needed sooner than
   planned if `.pluto` modules do more at load time.
7. **Should `.pluto` be the default in new projects?** **Resolved: yes.** New
   scripts are `.pluto` everywhere, including in pre-existing projects that never
   set `scriptExtension`. A project can still opt into `.lua` explicitly.
8. **`plutoc` on mobile/console exports.** The editor does not run there, so the
   executable should be excluded from those configs. (W5.1.)
9. **Strip level and stack traces.** Stripped bytecode degrades diagnostics;
   confirm the Debug/Release distinction. (W5.6.)
10. **`try`/`catch` deprecation.** If docs/templates encourage it, users get
    deprecation warnings. Prefer `pcall`/`xpcall` in generated content.

## Handoff checklist

Before starting:

- [x] Read `../Docs/pluto.md` and confirm the Pluto pin still matches
      `engine/libs/pluto/CMakeLists.txt`.
- [x] Build `doriax-editor` on Linux once to confirm a clean baseline.
- [x] Grep `".lua"` across `engine/ editor/` and re-confirm the file list above
      has not drifted (`grep -rn '\.lua' engine editor --include=*.cpp --include=*.h`).

When finishing:

- [x] Update `../Docs/pluto.md` (remove items this plan retires, add new caveats).
- [x] Update `AGENTS.md` if the build or hard rules changed.
- [x] Leave this plan's checkboxes accurate; if unfinished, keep the file and
      mark the current milestone.
- [x] Verify the test matrix rows you touched, and note which rows remain
      unverified (especially non-Linux).
- [x] If `plutoc` or bytecode shipped, record the Pluto revision in the export
      metadata and in the docs.
