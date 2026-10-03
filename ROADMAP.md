# ROADMAP — CP2077 story co-op

Rules: one task at a time · each task ends with an in-game test by the user · a phase is done
only when its **Exit criteria** pass · ⛔ = go/no-go gate.
Task format: **ID — title** · *Depends on* · Steps · **Done when** · Test.
Estimates assume one developer in spare time.

```
P0 Setup ─► P1 Feasibility ─► ⛔G1 ─► P2 Network ─► P3 Players ─► P4 Quest sync ─► P5 Scenes ─► ⛔G2 ─► P6 Combat/world ─► P7 Full story ─► P8 Release
                                │
                                └─(fail)─► Fallback: free-roam co-op = P2 + P3 + P6 only
```

---

## P0 — Environment setup (1–2 weeks)
Goal: working toolchain and a first native function callable in game.

- [ ] **P0.1 — Pin the game**
  Steps: back up game folder; disable auto-updates (Steam: "Only update when launched" + launch via
  exe; GOG: disable auto-update); record exact version in `docs/FINDINGS.md`.
  **Done when** version 2.31 is noted and backup exists.
- [ ] **P0.2 — Install frameworks**
  RED4ext, redscript, Cyber Engine Tweaks, Codeware (latest builds for 2.31).
  **Done when** game starts, CET overlay opens, `red4ext/logs/` and `r6/logs/redscript_rCURRENT.log` exist without errors.
- [ ] **P0.3 — Repo skeleton**
  Create layout from CLAUDE.md; submodules `vendor/RED4ext.SDK`, `vendor/RedLib`, `vendor/enet`;
  `.gitignore` (build/, dump/); CMake project `Coop` (shared lib, C++20, NOMINMAX); post-build copy
  to `$CP2077_DIR`.
  **Done when** `cmake --build` produces `Coop.dll` with no warnings-as-errors failures.
- [ ] **P0.4 — Hello plugin**
  RedLib plugin: `Main/Query/Supports`, `RTTI_DEFINE_GLOBALS({ RTTI_FUNCTION(CoopPing); })`,
  `CoopPing() -> String` returns `"pong <version>"`; `scripts/Coop/Core.reds` declares it;
  register script path via `aSdk->scripts->Add`.
  **Done when** CET console `print(CoopPing())` prints `pong 0.0.1`. Test: user runs it in CET.
- [ ] **P0.5 — CET mod skeleton**
  `cet/coop/init.lua` with `onInit`, `onDraw` ImGui window "Co-op" showing `CoopPing()` result,
  hotkey `coop_toggle_ui`.
  **Done when** window appears in CET overlay.
- [ ] **P0.6 — Decompiled scripts for research**
  User runs `redscript-cli decompile -b "<GAME>/r6/cache/final.redscripts" -o dump/scripts.reds`
  (or `-v`). Claude adds `tools/grep_dump.py` helper (search class / method / CName in dump).
  **Done when** `python tools/grep_dump.py QuestsSystem` lists its methods.
- [ ] **P0.7 — Logging**
  Own rotating log file `red4ext/plugins/Coop/logs/coop.log` (spdlog or simple), levels, timestamps,
  frame number. Redscript helper `CoopLog(msg)` → native.
  **Done when** a log line from C++, redscript and CET all land in one file.

**Exit criteria P0:** P0.1–P0.7 done; one build+deploy command; logs readable.

---

## P1 — Feasibility spike, single player, no network (2–4 weeks)
Goal: prove the 4 assumptions the whole mod stands on. Results go to `docs/FINDINGS.md`.

- [ ] **P1.1 — Fact logger**
  Hook `FactManager::SetFact(store, factHash, value)` (vtable slot 0x18, instance at
  `QuestsSystem + 0xF8`, see skill §8.1). Log `frame, store, hash, value, oldValue`. Map hashes to
  names where possible (build a `FNV1a32(name) → name` table from fact names found in `dump/` and
  quest files; unknowns stay hex).
  **Done when** playing *The Rescue* produces a readable log incl. graph stores 2–10.
- [ ] **P1.2 — Fact replay experiment** ⚠ key experiment
  Save A at mission start → play to the end with logger → reload save A → replay the logged
  `SetFact` sequence (all stores) via a CET command `Coop.ReplayFacts("file")`.
  Observe: does the quest graph advance (objectives, journal, NPC behavior)?
  Variants to try if not: replay only named store; replay + Codeware `QuestsSystem.ExecuteNode`
  on the waiting node.
  **Done when** FINDINGS.md has a clear YES / PARTIAL / NO with evidence.
- [ ] **P1.3 — Scene detection**
  Find reliable signals for scene start/end (candidates: PlayerStateMachine `SceneTier`,
  scene system events, workspot). Log `sceneStart{id/resource}`, `sceneEnd`.
  **Done when** every dialogue/cutscene in *The Pickup* logs start+end with a stable identifier.
- [ ] **P1.4 — Choice capture**
  Hook where the player confirms a dialogue choice (candidates: dialog UI controller,
  `UIInteractionsDef` blackboard, `gameinteractionsChoiceEvent`). Log hub id, option index,
  caption, TweakDB id.
  **Done when** every choice in *The Pickup* is logged with an id that is identical across two playthroughs.
- [ ] **P1.5 — Choice injection** ⚠ key experiment
  CET command `Coop.Choose(index)` that selects option N in the active choice hub without input.
  **Done when** FINDINGS.md: YES/NO + method; timed choices tested.
- [ ] **P1.6 — Quest trigger gating**
  Prototype delaying a quest trigger until a condition (e.g. a fact `coop_partner_near == 1`).
  Candidates: wrap trigger-area activation, pause condition node, `FactsDBCondition`.
  **Done when** a test trigger in *The Rescue* fires only after toggling the condition from CET.
- [ ] **P1.7 — Findings report**
  Summarize P1.1–P1.6 with recommendation: full story co-op / partial / free-roam only.

### ⛔ Gate G1 — is story co-op feasible?
- P1.2 = NO **and** P1.5 = NO → switch to **fallback** (free-roam co-op: P2, P3, P6) and stop here with story.
- P1.2 = PARTIAL → continue, plan heavier use of `ExecuteNode` + resync in P4.
- P1.5 = NO but P1.2 = YES → continue with "guest watches scene, facts applied after" model in P5.

---

## P2 — Network core (2–3 weeks)
Goal: two games connect and exchange messages without stutter or crashes.

- [ ] **P2.1 — Transport**
  ENet in `src/net/`: host (server) / client modes, channel 0 reliable-ordered, channel 1 unreliable
  sequenced. Dedicated network thread, 1 ms service loop.
- [ ] **P2.2 — Queues & tick**
  `IGameSystem CoopSystem` with `OnRegisterUpdates` → `FrameBegin` tick drains inbound queue
  (max N msgs/frame) and flushes outbound. No game calls from net thread (assert thread id).
- [ ] **P2.3 — Protocol v1**
  Binary header `{u16 protocol, u16 type, u32 seq, u32 len}`; messages `Hello, Welcome, Ping, Pong,
  Bye`. Document in `docs/PROTOCOL.md`. Unit tests for (de)serialization (GoogleTest, runs without game).
- [ ] **P2.4 — Handshake**
  `Hello{gameVersion, modVersion, protocol, saveHash, playerName}`; host rejects mismatch with readable reason.
- [ ] **P2.5 — API to scripts**
  RTTI class `CoopNet`: `Host(port)`, `Connect(ip, port)`, `Disconnect()`, `IsConnected()`,
  `GetPingMs()`, `GetRole()`. CET UI: host/join form, status, ping.
- [ ] **P2.6 — Resilience**
  Timeouts, reconnect, disconnect on save load/quit; plugin survives exceptions; game never blocks.
- [ ] **P2.7 — Loopback test harness**
  `tools/fake_peer.py` (or a small C++ console app) speaking protocol v1, so single-PC tests are possible.

**Exit criteria P2:** two PCs connect over LAN/IP, ping shown in UI, pulling the cable doesn't crash or freeze either game, unit tests green.

---

## P3 — Players in the world (3–4 weeks)
Goal: you see each other, partner movement looks smooth.

- [ ] **P3.1 — Transform sync** `Transform{pos, yaw, vel, flags}` at 20 Hz, unreliable channel.
- [ ] **P3.2 — Partner avatar** spawn via Codeware `DynamicEntitySystem` (`DynamicEntitySpec`,
  tag `n"Coop.Partner"`); pick a V-like character record or template; despawn on disconnect.
- [ ] **P3.3 — Interpolation** 100 ms buffer, snap if error > 5 m, extrapolate ≤ 200 ms.
- [ ] **P3.4 — Animation states** idle / walk / run / crouch / weapon drawn / in vehicle (flags).
- [ ] **P3.5 — Loading & fast travel** hide avatar during loading, respawn after `Session/Ready`
  or teleport events (Codeware `CallbackSystem`).
- [ ] **P3.6 — Teleport to partner** hotkey + CET button; safe position (navmesh/ground) — VERIFY API.
- [ ] **P3.7 — Player list & name tag** ImGui list; optional mappin for partner (MappinSystem).

**Exit criteria P3:** walk together through Watson for 10 min: no jitter, no avatar through walls at normal speed, avatar survives fast travel and loading.

---

## P4 — Shared quest progression (4–8 weeks)
Goal: both always have the same story state; the guest can resync to the host.

- [ ] **P4.1 — Fact replication** host: every `SetFact` (all stores) → `FactSet{store, hash, value}`
  reliable; guest: apply on game thread with "remote" flag so it isn't echoed back.
- [ ] **P4.2 — Guest suppression** guest's own fact writes from quest logic: forward to host
  (host decides) instead of applying locally — design per FINDINGS (may require hook ordering).
- [ ] **P4.3 — Join snapshot** host sends full fact stores on join (`FactSnapshot`, chunked).
- [ ] **P4.4 — Checksum & resync** every 10 s `FactChecksum` per store; mismatch → log diff +
  auto/hotkey `Resync`. `tools/fact_diff.py` compares two dumps.
- [ ] **P4.5 — Save model** only host saves; guest saving disabled with message; on join guest must load
  the host's save (`saveHash` check). Document workflow in `docs/TESTING.md`.
- [ ] **P4.6 — Proximity gate** quest triggers fire only when both players within N m (default 30);
  UI "Waiting for partner"; option to teleport partner. Built on P1.6.
- [ ] **P4.7 — Shared travel** fast travel / area transitions / elevators teleport both.
- [ ] **P4.8 — Journal/objective sync check** compare tracked quest + objectives on both after each FactSet burst.

**Exit criteria P4:** complete *The Rescue* together without manual intervention; identical checksums at the end.

---

## P5 — Shared scenes & dialogue (4–8 weeks)
Goal: cutscenes play on both, one player chooses, the other sees the choice.

- [ ] **P5.1 — Scene start sync** `SceneStart{sceneId}`: second client waits/starts in step (both within gate).
- [ ] **P5.2 — Decider rule** setting: scene initiator decides (default) or alternate per scene.
- [ ] **P5.3 — Choice relay** decider's `ChoiceMade{hubId, index}` → other client injects (P1.5).
  If injection impossible: other client gets facts after scene (fallback from G1).
- [ ] **P5.4 — Non-decider UI** "Partner is choosing…", then show chosen line.
- [ ] **P5.5 — Timed & silent choices** timeout handling, "no answer" outcome identical on both.
- [ ] **P5.6 — Edge cases** holocalls, phone calls, Johnny scenes, braindance, sex/romance scenes
  (non-decider gets fade/skip), vehicle dialogues. Each case documented in FINDINGS.
- [ ] **P5.7 — Desync recovery** scene finished on one side only → resync facts + `ExecuteNode` if needed.

### ⛔ Gate G2 — does one full mission work end-to-end?
*The Pickup* played start to finish together, all conversations, both end in the same branch,
no manual resync. Only then continue.

---

## P6 — Combat, rewards, world (4–6 weeks)
- [ ] **P6.1 — Enemy authority** host owns health/death of quest-relevant NPCs (persistent IDs);
  guest damage sent to host as `DamageRequest`.
- [ ] **P6.2 — Death sync** `EnemyState{persistentId, hp, dead}`; ambient crowd stays local.
- [ ] **P6.3 — Rewards** quest items, money, XP to both (TransactionSystem / PlayerDevelopment) — VERIFY APIs.
- [ ] **P6.4 — Time & weather** host-driven `gameTimeSystem` + Codeware `WeatherSystem`.
- [ ] **P6.5 — Vehicles** v1: each drives own car; v2: passenger seat — research separately.
- [ ] **P6.6 — Player death** downed state → partner revive, or respawn near partner.

**Exit criteria P6:** a quest shootout (e.g. *The Pickup* Maelstrom fight) ends with the same dead enemies and the same rewards on both.

---

## P7 — Full story pass (3–6 months, iterative)
- [ ] **P7.1 — Prologue & Act 1** play, log, fix; `docs/KNOWN_ISSUES.md` per quest.
- [ ] **P7.2 — Act 2 main line**
- [ ] **P7.3 — Side quests affecting endings** (Panam, Judy, River, Kerry, Rogue…)
- [ ] **P7.4 — Act 3 & endings**
- [ ] **P7.5 — Phantom Liberty** (optional, separate pass)
Workflow per stuck quest: user sends logs → Claude diagnoses diverged facts/graph nodes → targeted
fix or documented resync step.

---

## P8 — Release (2–3 weeks)
- [ ] **P8.1 — Startup checks** game version, RED4ext/redscript/CET/Codeware presence & versions, clear error UI.
- [ ] **P8.2 — Packaging** Nexus-ready zip mirroring game folder layout; no game files.
- [ ] **P8.3 — Docs** install, hosting, joining, port forwarding, known issues (CZ + EN).
- [ ] **P8.4 — External test** a person unfamiliar with the project installs and joins using docs only.

---

## Risk register
| Risk | Impact | Shows in | Fallback |
|---|---|---|---|
| Choice can't be injected | high | P1.5 | guest watches, facts applied after scene |
| Fact replay doesn't advance graph | high | P1.2 / P4 | `ExecuteNode`, load host save |
| Scenes drift in time | medium | P5 | sync points at scene start + each choice |
| Game patch breaks plugin | medium | any | pinned version, hashes/RTTI over offsets |
| NPCs lack shared IDs | medium | P6 | sync only persistent story NPCs |
