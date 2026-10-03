# CP2077 Co-op — project instructions for Claude Code

2-player **story co-op** mod for Cyberpunk 2077. Both players do every quest together,
scenes play on both clients, one player picks dialogue choices, the host is authoritative.

## Always
- Load the skill `cyberpunk-red4ext-modding` (in `.claude/skills/`) before writing any game code.
- Follow `ROADMAP.md`. Work on **one task ID at a time** (e.g. `P2.3`). Don't start the next phase
  until the current phase's exit criteria are met and the user confirmed testing in game.
- Talk to the user in **Czech**; code, comments, commit messages and docs in **English**.
- The user tests in the game himself. After each task, tell him exactly what to test, where to look
  (which log file, which CET command) and what the expected result is.
- Never invent game APIs. Verify in `dump/` (decompiled scripts), NativeDB, SDK headers or Codeware
  sources. If unsure, mark with `// VERIFY:` and say so.
- Record verified/disproved facts about the game in `docs/FINDINGS.md` (with evidence).
- Keep `docs/PROTOCOL.md` in sync with every network message change (bump protocol version).
- Update the task checkbox in `ROADMAP.md` when a task is done and tested.

## Hard rules
- Game objects only on the game thread. Network thread ↔ queue ↔ `FrameBegin` tick.
- The game must keep running if the network or the plugin fails (no crash, no hang).
- Pinned game version: **2.31** (`RED4EXT_V1_RUNTIME_VERSION_2_31`). Do not use raw offsets
  when an RTTI call or address-library hash exists.
- Prefer `@wrapMethod` over `@replaceMethod`. Prefer RTTI calls over Detours hooks.
- Never edit quest facts by hand except through the replication path; never ship game files.

## Repo layout (target)
```
src/            C++ RED4ext plugin (RedLib)
  net/          ENet transport, thread, queues, protocol (de)serialization
  sync/         facts, transform, scenes, combat replication
  game/         IGameSystem(s), hooks, RTTI bindings
scripts/        redscript (.reds), module Coop.*
cet/coop/       CET Lua mod (debug UI, hotkeys) -> bin/x64/plugins/cyber_engine_tweaks/mods/coop
tools/          log analyzers, fact-diff tool (Python)
vendor/         RED4ext.SDK, RedLib, enet (git submodules)
dump/           decompiled game scripts (git-ignored, local only)
docs/           FINDINGS.md, PROTOCOL.md, KNOWN_ISSUES.md, TESTING.md
```

## Build & deploy
- CMake ≥ 3.23, Visual Studio 2022+, C++20, `NOMINMAX`.
- `cmake -B build -G "Visual Studio 17 2022" && cmake --build build --config RelWithDebInfo`
- Post-build copies: `Coop.dll` + `scripts/` → `<GAME>/red4ext/plugins/Coop/`, `cet/coop/` →
  `<GAME>/bin/x64/plugins/cyber_engine_tweaks/mods/coop/`. `<GAME>` comes from env var `CP2077_DIR`.

## Logs to ask the user for
- `<GAME>/red4ext/logs/` (plugin), `<GAME>/r6/logs/redscript_rCURRENT.log`,
  `<GAME>/bin/x64/plugins/cyber_engine_tweaks/mods/coop/coop.log`, our own `Coop/logs/*.log`.

## Slash commands
- `/next-task` – pick the next open task from ROADMAP.md and plan it.
- `/test-plan` – write the in-game test steps for the last finished task.
