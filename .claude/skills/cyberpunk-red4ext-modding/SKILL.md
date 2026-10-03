---
name: cyberpunk-red4ext-modding
description: Cyberpunk 2077 modding přes RED4ext (C++ SDK, RedLib, red4ext-rs), redscript a Cyber Engine Tweaks — pluginy, hooky, quest facts, scény, spawn entit, síťový co-op mod. Použij vždy, když jde o kód pro Cyberpunk 2077.
---

# Cyberpunk 2077 modding: RED4ext · redscript · CET

Reference distilled from the source code of RED4ext, RED4ext.SDK, RedLib, Codeware, ArchiveXL,
redscript, Cyber Engine Tweaks, red4ext-rs and the redmodding wiki (state: October 2026,
game patch 2.31 = latest runtime in SDK). Primary use case of this skill: building a
**2-player story co-op mod** (shared quest progression, both players present, synced scenes).

## 0. Golden rules (read first)

1. **Never invent game APIs.** Cyberpunk has ~30k RTTI types. If a function/field is not
   confirmed by this skill, by NativeDB, by decompiled game scripts or by a CET `Dump()`,
   say so explicitly and give the user a one-line way to verify it (see §9).
   Mark uncertain things in code with `// VERIFY:`.
2. **Game thread only.** Never touch game objects (entities, systems, handles) from a network
   or worker thread. Network thread → lock-free/mutex queue → drained on game tick (§4.4).
3. **Pin the game version.** Plugins declare `runtime`; patches can break offsets/vtables.
   Prefer address-library hashes and RTTI calls over raw offsets.
4. **Prefer RTTI over hooks.** Call/extend things via RTTI (scriptable functions, redscript
   `@wrapMethod`) before reaching for Detours hooks on raw addresses.
5. **Graceful failure.** If the plugin/network fails, the game must keep running normally.
6. Facts are dangerous: the wiki warns that editing quest facts “will almost always cause
   problems much later in the game”. Sync *all* fact changes faithfully; never hand-edit.

## 1. Ecosystem map

| Layer | What | Language | Install location |
|---|---|---|---|
| **RED4ext** (loader) | Injects via proxy `winmm.dll` in `bin/x64`, loads `red4ext.dll`, manages plugins, hooking, logging, redscript path injection | C++ | `red4ext/` + `bin/x64/winmm.dll` |
| **RED4ext.SDK** | Header-only (or static, `RED4EXT_STATIC_LIB`) C++20 lib: reversed engine types, RTTI system, ~680 generated native type folders (`Scripting/Natives/Generated/*`) | C++20 | your project (`vendor/`) |
| **RedLib** (psiberx) | Header-only helpers over the SDK: `RTTI_DEFINE_CLASS`, `RTTI_DEFINE_GLOBALS`, `Red::CallVirtual`, `Red::GetGameSystem<>` … `Red` = alias of `RED4ext` namespace | C++20 | `vendor/RedLib` |
| **red4ext-rs** | Rust bindings (`red4ext-rs` + `red4ext-rs-bindings` for all game types) | Rust | DLL → `red4ext/plugins/` |
| **redscript** | Statically typed scripting language compiled to game bytecode (`final.redscripts`) at startup | `.reds` | `r6/scripts/<Mod>/` |
| **Cyber Engine Tweaks (CET)** | Lua 5.1 (LuaJIT) runtime + ImGui overlay; calls anything in RTTI | Lua | `bin/x64/plugins/cyber_engine_tweaks/mods/<mod>/init.lua` |
| **Codeware** | RED4ext+redscript library: `DynamicEntitySystem`, `CallbackSystem`, `QuestsSystem.ExecuteNode`, reflection, UI | C++/reds | dependency |
| **ArchiveXL / TweakXL** | Resource and TweakDB extensions (YAML), quest-phase injection | C++ | dependency |
| **WolvenKit** | Editor for `.archive`, `.questphase`, `.scene`, `.ent` files | GUI | dev tool |

Logs: RED4ext `red4ext/logs/`, redscript `r6/logs/redscript_rCURRENT.log`,
CET `bin/x64/plugins/cyber_engine_tweaks/scripting.log` + `mods/<mod>/<mod>.log`.

Key interop fact: **anything registered in RTTI (by a RED4ext plugin) is callable from
redscript (after a `native` declaration) AND from CET Lua** automatically.

## 2. RED4ext plugin — raw SDK (API v1)

Exports every plugin must have: `Main`, `Query`, `Supports`.

```cpp
#include <RED4ext/RED4ext.hpp>

RED4EXT_C_EXPORT void RED4EXT_CALL RegisterTypes() { /* register new classes/enums */ }

RED4EXT_C_EXPORT void RED4EXT_CALL PostRegisterTypes()
{
    auto rtti = RED4ext::CRTTISystem::Get();
    auto func = RED4ext::CGlobalFunction::Create("CoopPing", "CoopPing", &CoopPing);
    func->flags = {.isNative = true, .isStatic = true};
    rtti->RegisterFunction(func);
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle,
                                        RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        auto rtti = RED4ext::CRTTISystem::Get();
        rtti->AddRegisterCallback(RegisterTypes);
        rtti->AddPostRegisterCallback(PostRegisterTypes);
        aSdk->scripts->Add(aHandle, L"scripts");        // ship .reds next to the DLL
        aSdk->logger->Info(aHandle, "Coop loaded");
        break;
    }
    case RED4ext::v1::EMainReason::Unload: break;
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"Coop";
    aInfo->author = L"Jan";
    aInfo->version = RED4EXT_V1_SEMVER(0, 1, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;   // or a pinned RED4EXT_V1_RUNTIME_VERSION_2_31
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports() { return RED4EXT_API_VERSION_1; }
```

`aSdk` (`RED4ext::v1::Sdk`) members: `runtime` (SemVer of game), `logger` (Trace/Debug/Info/Warn/
Error/Critical + `F`/`W`/`WF` variants), `hooking` (`Attach(handle, target, detour, &original)`,
`Detach`), `gameStates` (`Add(handle, EGameStateType, GameState*)` with OnEnter/OnUpdate/OnExit),
`scripts` (`Add` path to redscript compilation, `RegisterNeverRefType`, `RegisterMixedRefType`).
Older docs/plugins use `RED4ext::Sdk`/`RED4ext::EMainReason` without `v1::` — same ABI.

Raw native function signature (manual stack handling):

```cpp
void CoopSend(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::CString msg;
    RED4ext::GetParameter(aFrame, &msg);   // read params in order
    aFrame->code++;                        // ALWAYS skip ParamEnd after reading params
    if (aOut) *aOut = Enqueue(msg.c_str());
}
```

Runtime version defines live in `Api/v1/Runtime.hpp` (… `2_21`, `2_30`, `2_31`; `LATEST`;
`INDEPENDENT` only if you don't use SDK types).

## 3. RedLib — recommended way to write plugins

```cpp
#include <RedLib.hpp>

struct CoopNet : Red::IScriptable
{
    bool Connect(Red::CString host, int32_t port);
    bool Send(Red::CString channel, Red::CString payload);
    Red::DynArray<Red::CString> Poll();          // drained on game thread
    RTTI_IMPL_TYPEINFO(CoopNet);
    RTTI_IMPL_ALLOCATOR();
};
RTTI_DEFINE_CLASS(CoopNet, { RTTI_METHOD(Connect); RTTI_METHOD(Send); RTTI_METHOD(Poll); });

RTTI_DEFINE_GLOBALS({ RTTI_FUNCTION(CoopVersion); });
RTTI_DEFINE_ENUM(ECoopRole);                      // enums / RTTI_DEFINE_FLAGS for bitflags

// in Main on Load:
Red::TypeInfoRegistrar::RegisterDiscovered();
```

Matching redscript declarations:

```swift
public native class CoopNet {
  public native func Connect(host: String, port: Int32) -> Bool
  public native func Send(channel: String, payload: String) -> Bool
  public native func Poll() -> array<String>
}
```

RedLib features worth knowing:
- `Red::Optional<T, Default>` params → `opt` in redscript; `Red::ScriptRef<T>` → `script_ref<T>`.
- Struct instance methods become static `(self: script_ref<S>, …)` in redscript.
- `RTTI_PERSISTENT(field)` → saved in the save file (with `persistent` field in a ScriptableSystem).
- **Game systems:** a class deriving `Red::IGameSystem` registered with `RTTI_DEFINE_CLASS` is
  auto-instantiated per game instance; expose it with
  `@addMethod(GameInstance) public static native func GetCoopSystem() -> ref<CoopSystem>`.
  Overridable virtuals: `OnWorldAttached/OnBeforeWorldDetach/OnWorldDetached`, `OnGameSave`,
  `OnGameLoad`, `OnGameRestored`, `OnGamePrepared`, `OnGamePaused/Resumed`,
  `OnStreamingWorldLoaded`, `OnInitialize/OnUninitialize`.
- Scripted classes backed by native code: derive `Red::ScriptableSystem`, use
  `RTTI_FWD_CONSTRUCTOR()` + `RTTI_SCRIPT_METHOD(OnAttach)` etc.
- Extend existing classes: `RTTI_EXPAND_CLASS(Red::GameObject, { RTTI_METHOD_FQN(Ext::Fn); })`
  (methods only — properties cannot be added to existing native classes).
- Raw handlers: method `(Self*, CStackFrame*, Out*, CBaseRTTIType*)` or add `Red::CStackFrame*` as last param.
- Type helpers: `Red::GetTypeName<T>()`, `Red::GetType<T>()`, `Red::GetClass<T>()`.
- Logging: `Red::Log::Debug("x={}", x)`.

## 4. Calling into the game from C++

### 4.1 Calling functions
```cpp
// RedLib
Red::ScriptGameInstance game;
Red::Handle<Red::PlayerSystem> ps;
Red::Handle<Red::GameObject> player;
Red::CallStatic("ScriptGameInstance", "GetPlayerSystem", ps, game);
Red::CallVirtual(ps, "GetLocalPlayerControlledGameObject", player);
Red::CallGlobal("MaxF", out, a, b);

// Raw SDK
RED4ext::ExecuteFunction("gameTimeSystem", "GetGameTime", &gameTime);            // singleton
RED4ext::ExecuteGlobalFunction("GetPlayer;GameInstance", &handle, gameInstance); // mangled global
RED4ext::ExecuteFunction(handle, cls->GetFunction("GetQuickSlotsManager"), &out);
```
Native systems directly: `Red::GetGameSystem<Red::IPersistencySystem>()`.

### 4.2 Address library & hooks
- Since 2.0 RED4ext loads `bin/x64/cyberpunk2077_addresses.json` and resolves **stable hashes**
  to addresses: `RED4ext::UniversalRelocFunc<T>(hash)`, `UniversalRelocPtr<T>`, `UniversalRelocVtbl`.
  Codeware's `src/Red/Addresses/Library.hpp` is the best catalogue of known hashes
  (e.g. `QuestsSystem_CreateContext = 3144298192`, `QuestPhaseInstance_ExecuteNode`, `EntitySpawner_SpawnEntity`).
- Hook: `aSdk->hooking->Attach(aHandle, target, &Detour, reinterpret_cast<void**>(&Original));`
  Detach on Unload. Hooks use Microsoft Detours.
- Virtual function hook: read vtable from a live instance, take slot `offset/8`, attach to that address.

### 4.3 Threading
Script VM, entities and systems are **game-thread only**. Network I/O runs on its own thread.

### 4.4 Per-frame tick (where to drain the network queue)
```cpp
struct CoopSystem : Red::IGameSystem   // IGameSystem derives from IUpdatableSystem
{
    void OnRegisterUpdates(Red::UpdateRegistrar* r) override
    {
        r->RegisterUpdate(Red::UpdateTickGroup::FrameBegin, this, "CoopSystem/Tick",
                          {this, &CoopSystem::OnTick});
    }
    void OnTick(Red::FrameInfo& frame, Red::JobQueue&) { /* drain inbound queue, apply */ }
};
```
(Pattern used by Codeware `StaticEntitySystem`.) Tick groups: `FrameBegin, EntityUpdateState,
PreBuckets, Buckets, PostBuckets, CameraUpdate, PlayerAimUpdate, …, PreRenderUpdate`.
Fun fact: the enum also contains `Multiplayer_UpdateStateSnapshots` / `Multiplayer_CaptureStateSnapshots`
— remnants of CDPR's cut multiplayer (also `quest/Multiplayer*` node types, `net/EntityReplicatedState`,
`ReplAnimTransform*Request`). Treat as dead code; do not rely on it working.

## 5. redscript essentials

```swift
module Coop.Core
import Coop.Net.*

@wrapMethod(PlayerPuppet)                       // chainable, mod-compatible hook
protected cb func OnGameAttached() -> Bool {
  let r = wrappedMethod();                      // call original (exact same args)
  CoopLog("player attached");
  return r;
}
@replaceMethod(Cls)    // full replace, only ONE mod can win – avoid
@addMethod(Cls)        // new method on existing class
@addField(Cls) let f: Int32;                   // new field
@replaceGlobal() public static func X() {}     // replace global func
@if(ModuleExists("Codeware")) import Codeware.*
```
- Types: `ref<T>`, `wref<T>`, `script_ref<T>`, `array<T>`, `CName n"x"`, `TweakDBID t"Items.x"`,
  `ResRef r"base\\x.ent"`, `s"interp \(v)"`, `Variant`. Cast: `x as T` + `IsDefined(x)`. No `continue`.
- `ScriptableSystem` (`OnAttach`, `OnDetach`, `OnRestored(saveVersion, gameVersion)`), fetched with
  `GameInstance.GetScriptableSystemsContainer(game).Get(n"Module.ClassName") as ClassName`.
  `persistent` fields go into the save.
- Events: `protected cb func OnSomething(evt: ref<SomeEvent>) -> Bool`; send with `obj.QueueEvent(evt)`.
- Delays: subclass `DelayCallback` with `Call()`; `GameInstance.GetDelaySystem(game).DelayCallback(cb, 1.0, false)`.
- Logging: `LogChannel(n"DEBUG", "…")`, `FTLog`. Since 2.01 log natives may need declaring.
- Scripts in `r6/scripts/`; compile errors show at startup and in `redscript_rCURRENT.log`.
- Tooling: `redscript-ide` (LSP + debugger for VSCode/neovim/Zed…), `redscript-cli decompile -b final.redscripts -o out`.

## 6. Cyber Engine Tweaks (Lua)

```lua
registerForEvent('onInit', function()
  Observe('PlayerPuppet', 'OnGameAttached', function(self) print('attached') end)  -- = ObserveBefore
  ObserveAfter('QuestsSystem', 'SetFactStr', function(self, name, value) end)       -- VERIFY signature
  Override('Cls', 'Method', function(self, arg, wrapped) return wrapped(arg) end)
end)
registerForEvent('onUpdate', function(dt) end)       -- every frame
registerForEvent('onDraw', function() if ImGui.Begin('Coop') then ImGui.Text('ok') end ImGui.End() end)
registerForEvent('onShutdown', function() end)        -- also onOverlayOpen/Close, onTweak
registerHotkey('coop_resync', 'Resync from host', function() end)   -- registerInput for press/release
return { api = {} }                                   -- other mods: GetMod('coop').api
```
- Globals: `Game.*` (e.g. `Game.GetPlayer()`, `Game.GetQuestsSystem()`, `Game.GetTeleportationFacility()`),
  `GetSingleton('gameTimeSystem')`, `NewObject('ClassName')`, `ToVector4{x=,y=,z=,w=1}`,
  `ToEulerAngles{}`, `TweakDBID.new('…')`, `CName.new('…')`, `IsDefined`, `Dump/DumpType/GameDump`.
- **Sandbox:** whitelisted libs only (incl. `json`, `bit32`); file I/O only inside the mod folder;
  **no sockets / no network** → networking must live in a RED4ext plugin and be called from Lua via RTTI.
- Good for: prototyping, debug UI (ImGui), quick hooks. Ship-critical logic → redscript/C++.

## 7. red4ext-rs (Rust alternative)

```rust
use red4ext_rs::{export_plugin_symbols, exports, global, wcstr, Exportable, GlobalExport, Plugin, SemVer, U16CStr};
pub struct Coop;
impl Plugin for Coop {
    const AUTHOR: &'static U16CStr = wcstr!("Jan");
    const NAME: &'static U16CStr = wcstr!("coop");
    const VERSION: SemVer = SemVer::new(0, 1, 0);
    fn exports() -> impl Exportable { exports![GlobalExport(global!(c"CoopPing", ping))] }
}
export_plugin_symbols!(Coop);
fn ping() -> i32 { 1 }
```
Cargo: `crate-type = ["cdylib"]`, deps `red4ext-rs` (+ `red4ext-rs-bindings`). Good fit for async
networking (tokio/quinn). Use C++/RedLib if you need deep reversed internals (Codeware-style).

## 8. Game internals for co-op (verified vs. to-verify)

### 8.1 Quest facts — the core of progression sync  ✅ verified (Codeware/ArchiveXL source)
- A fact = name hash (`FactID`, FNV1a32 of the string) → `int32`. Unset = 0.
- `FactManager` (at `QuestsSystem + 0xF8`) has **11 stores**: index 1 = `NamedFactStore`
  (normal named facts), **2–10 = graph stores** keyed by `QuestNodeKey` (phase-path hash + node id).
  Quest graph node state (e.g. Flow Control counters “stored in a fact scoped to the Phase”) lives there.
- Virtuals: `GetFact(store, fact)` @ vtbl 0x10, `SetFact(store, fact, value)` @ vtbl 0x18.
- ⇒ **Strategy:** hook `FactManager::SetFact` on the host and replicate *every* store/fact/value
  (not only named facts). This likely also replicates quest-graph progress. Verify in Phase 0.
- Script level (✅ used in CET-kit): `Game.GetQuestsSystem():GetFactStr("name")`,
  `:SetFactStr("name", v)`. `GetFact(n"name")`/`SetFact(n"name", v)` with CName exist too.
  Listener `RegisterListener(factName: CName, target, fnName)` — **VERIFY signature**.
- Codeware: `@addMethod(QuestsSystem) native func ExecuteNode(node: ref<questNodeDefinition>, opt inputSocket: CName)`
  — can push a quest node manually (useful to un-stick a guest).
- Quest/scene files: `.quest` → `.questphase` graphs → `.scene`; fact names usually prefixed by
  quest code (`mq055_…`, `sq027_…`). Inspect with WolvenKit (“visualize quest files” guide).

### 8.2 Entities & avatars  ✅
- Codeware `DynamicEntitySystem`: `CreateEntity(spec)` with `DynamicEntitySpec { recordID | templatePath,
  appearanceName, position: Vector4, orientation: Quaternion, persistState, persistSpawn, alwaysSpawned,
  spawnInView, active, tags }`; `GetEntity(id)`, `DeleteEntity`, `GetTagged(tag)`, listeners per tag.
  → spawn partner proxy NPC (e.g. a V-lookalike character record), tag `n"Coop.Partner"`.
- Player transform: `player:GetWorldPosition()`, `GetWorldOrientation()`, `GetWorldYaw()`.
  Teleport: `Game.GetTeleportationFacility():Teleport(entity, pos, EulerAngles)`.
- Codeware `CallbackSystem` events: `Session/BeforeStart, Start, Ready, Pause, Resume, BeforeSave, Save,
  AfterSave, BeforeEnd, End`, `Entity/Initialize, Attach, Attached, Detach, Uninitialize, …`, `Input/Key, Input/Axis`.
  `GameInstance.GetCallbackSystem().RegisterCallback(n"Session/Ready", this, n"OnReady")`.

### 8.3 Scenes & dialogue choices  ⚠️ partly to-verify
- Native types: `scnSceneSystem`/`gameISceneSystem`, `scnChoiceNode` (+ `ChoiceNodeOption`,
  `ChoiceHubPartId`, timed choices), `scnDialogLineEvent`.
- Interaction layer: `gameinteractionsChoice { caption, captionParts, data: array<Variant>, choiceMetaData{tweakDBName,
  tweakDBID, type} }`, `gameinteractionsChoiceEvent { choice, actionType }`, `ChoiceType` flags
  (QuestImportant, AlreadyRead, Inactive, CheckSuccess/Failed, TimedDialog, Blueline, Pay, Selected, Illegal…).
- Leads to investigate for **reading/injecting choices** (VERIFY by decompiling): the dialogue UI
  controller (`dialogWidgetGameController` / `InteractionUIBase`) and the `UIInteractionsDef`
  blackboard (choice hubs, active hub, selected index); the event that is fired when the player confirms a choice.
- Scene presence: `PlayerStateMachine` blackboard `SceneTier` (tier ≥ 2 ≈ in scene) — VERIFY.

### 8.4 Other systems
- `TransactionSystem` (`gameITransactionSystem`) — items give/remove/equip.
- `gameTimeSystem` (`GetGameTime`, `SetGameTimeBySeconds`), weather via Codeware `WeatherSystem`.
- Journal: Codeware `JournalManager.GetEntries(request)`; `gameJournalQuest/Objective` types for active quest UI.

## 9. How to discover/verify APIs (do this instead of guessing)

1. **Decompile game scripts:** `redscript-cli decompile -b "<game>/r6/cache/final.redscripts" -o scripts_dump`
   (or `-v`); grep for class/func names. Authoritative for every scripted function and native declaration.
2. **NativeDB** (nativedb.red4ext.com) — browsable RTTI: classes, props, functions, enums.
3. **CET console:** `Dump(obj)`, `DumpType("ClassName")`, `GameDump(obj)`; `print(Game.GetPlayer():GetWorldPosition())`.
4. **SDK headers:** `RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/<ns>/<Type>.hpp` (layouts + NAME/ALIAS;
   C++ name `quest::FactsDBCondition` ↔ RTTI `questFactsDBCondition`).
5. **Codeware/ArchiveXL `src/Red/*.hpp`** for reversed internals (QuestsSystem, EntitySpawner, RuntimeScene…).
6. **WolvenKit** for `.questphase`/`.scene` graphs and fact names; wiki “Quest and Scene Node Definitions”.
7. RED4.RTTIDumper (rayshader) for fresh RTTI dumps after a patch.

## 10. Co-op mod reference architecture

```
[Game A: host]                                    [Game B: guest]
 CET Lua (debug UI, hotkeys)                       CET Lua
 redscript (hooks: scenes, choices, triggers)      redscript
 RED4ext plugin  ─── UDP (ENet/GNS) / QUIC ───────  RED4ext plugin
   net thread ⇄ queue ⇄ FrameBegin tick              net thread ⇄ queue ⇄ tick
```
- Host is authoritative: save file, facts (all stores), quest graph, time, weather, combat outcomes.
- Messages (versioned binary, document in PROTOCOL.md): `Hello{gameVersion, modVersion, saveHash}`,
  `Transform{pos, yaw, vel, animState} @20Hz` (interpolate ~100 ms), `FactSet{store, factHash, value}`,
  `FactSnapshot{…}` + periodic `FactChecksum`, `SceneStart{sceneId}`, `ChoiceMade{hubId, index}`,
  `Teleport{pos}`, `EnemyState{persistentId, hp, dead}`, `Resync`.
- Rules for story co-op: proximity gate (both players within N m before quest triggers fire),
  scenes run on both clients, one player picks choices → injected on the other, fast travel moves both,
  guest syncs to host state on join, checksum mismatch → resync from host.
- Phase 0 (single player, no networking): (1) log every `SetFact` incl. graph stores during one mission;
  (2) detect scene start/end + choice selection; (3) can a choice be selected programmatically?
  (4) can a quest trigger be delayed until a condition (proximity gate)? Only then build networking.
- Test with two PCs (or two accounts/installs); one PC cannot run two game instances.
- Non-commercial only; never redistribute game files.

## 11. Build & project setup

- C++: Visual Studio 2022+, CMake ≥ 3.23, C++20. `git submodule add` RED4ext.SDK (+ RedLib) into `vendor/`.
  `add_compile_definitions(NOMINMAX)`, `target_link_libraries(Coop PRIVATE RedLib)`.
- Output DLL → `<game>/red4ext/plugins/Coop/Coop.dll`; ship `.reds` in the same folder and register
  with `aSdk->scripts->Add(aHandle, L"scripts")`, or put them in `r6/scripts/Coop/`.
- Dependencies for users: RED4ext, redscript, CET, Codeware (+ ArchiveXL/TweakXL if needed).
- Rust: `cargo build --release`, copy DLL to `red4ext/plugins/`.

## 12. Pitfalls checklist

- Forgot `aFrame->code++` after reading params → stack corruption/crash.
- Registered functions must also be declared `native` in redscript (names/types must match RTTI).
- `@replaceMethod` conflicts between mods → use `@wrapMethod`.
- Holding `ref<>` to entities after detach → leaks/crashes; use `wref<>` or EntityID + lookup.
- Touching game state off the game thread → random crashes.
- After a game patch: check `RED4EXT_V1_RUNTIME_VERSION_*`, update SDK/RedLib/Codeware, re-verify hashes.
- CET `Observe` of a non-existent function fails silently-ish — check `scripting.log`.
- Module-qualified class names in CET/`Get(n"…")`: `n"Coop.Core.CoopSystem"`.
