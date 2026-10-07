# Migrating `.lua` scripts to Pluto (reserved words)

Status as of Pluto 0.12.2. Read [`pluto.md`](pluto.md) first for how the runtime
is wired.

## Why a `.lua` file can stop parsing

The engine runs Pluto, a Lua 5.4 superset. Pluto treats a set of names as
keywords that vanilla Lua allowed as identifiers. A script that was valid Lua
5.4 can therefore fail to load with no change to the file:

```lua
local new = 1            -- was valid Lua, now: unexpected symbol near 'new'
local t = { class = 2 }  -- was valid Lua, now: 'class' is a non-portable name
```

The failure is reported on the editor **Scripts** Output channel on save, and in
the engine log on Play.

## The reserved words

| | | | |
| --- | --- | --- | --- |
| `class` | `switch` | `case` | `default` |
| `enum` | `new` | `continue` | `parent` |
| `export` | `try` | `catch` | `global` |
| `as` | `begin` | `extends` | `instanceof` |
| `pluto_use` | | | |

The `pluto_*` compatibility spellings (`pluto_class`, `pluto_switch`, ...) are
reserved too, but they mean the same thing and are never what a migrated file
meant to use as a plain name.

The stock Lua keywords (`and`, `break`, `do`, ... `while`) are reserved in both
dialects; they are not part of this migration.

## Fixes

1. **Rename the identifier.** Preferred: it is local, obvious, and keeps the
   file portable. `local new = 1` becomes `local instance = 1`.
2. **Disable the keyword with `pluto_use`.** When a name must stay (a public
   API, a serialized field), turn the keyword off at the top of the file:

   ```lua
   pluto_use class = false
   pluto_use new = false

   local new = 1
   ```

   `pluto_use * = false` disables every optional keyword, and
   `pluto_use "0.8.0"` selects a whole compatibility era. Both are blunt: they
   also turn off the syntax the file may be relying on, so prefer the targeted
   form.
3. **Keep the file as `.lua` and leave it alone.** The extension is not what
   makes Pluto parse it; the runtime always uses Pluto. A `.lua` file with a
   reserved-word collision fails exactly like a `.pluto` one. Renaming to
   `.pluto` does not fix it.

Pluto can also warn instead of failing for some of these (`non-portable name`
/ `non-portable keyword usage`). Warnings come from the parser as text; the
engine surfaces them through the same channels as errors.

## Why the fork does not auto-fix

Rewriting an identifier changes behaviour in ways a text pass cannot see:
`{ class = ... }` may be a table key read elsewhere as a string, and a global
`new` may be referenced from a module not being scanned. Report first; let the
author rename.

## Working alongside `.lua` files

- Pluto parses `.lua` unchanged, so an existing project keeps working until it
  trips one of the names above.
- New scripts default to `.pluto` (`scriptExtension` in `project.yaml`); an
  older project without that key keeps creating `.lua`.
- When `enemy.pluto` and `enemy.lua` both exist, `.pluto` wins and the engine
  logs one warning naming both files. Delete the loser.
