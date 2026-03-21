# Effects Reverse Engineering Notes

This document captures the initial reverse engineering work done against the
Mac symbolized SC4 binary in the active Ghidra session on port `8193`
(`x86-32-cpu0x3` in project `MacSC4`).

The main target in this pass was the string `main.fx` and the startup path that
loads and wires up the stock Effects subsystem.

## Summary

The central object is `cSC4EffectsManager`. Its startup method
`cSC4EffectsManager::Init()` calls `cSC4EffectsManager::LoadAllEffects()`,
which is the canonical bootstrap path for effect definitions.

The important strings in the symbolized binary are:

- `0x005E09BC`: `packedeffects`
- `0x005E09CC`: `main.fx`
- `0x005E09D4`: `Effects/config.fx`

`LoadAllEffects()` supports two loading modes:

- packed mode: load the built-in packed effects resource set, then optionally
  parse plugin `main.fx`
- unpacked mode: parse `Effects/config.fx` from the game data directory, then
  parse plugin `main.fx`

After parsing, the manager builds effect lookup tables and message-trigger maps,
registers script-facing hooks, and exposes runtime creation through the
game-data registry.

## Key Functions

- `cSC4EffectsManager::Init()` at `0x0040A2DE`
- `cSC4EffectsManager::LoadAllEffects()` at `0x00409E36`
- `cSC4EffectsManager::ParseEffectsFiles()` at `0x00403ECC`
- `cSC4EffectsManager::BeginCollection()` at `0x00404B86`
- `cSC4EffectsManager::EndCollection()` at `0x00405270`
- `cSC4EffectsManager::DoMessage(cIGZMessage2*)` at `0x0040A90A`
- `cSC4EffectsManager::DoEffectsTick()` at `0x00406C12`
- `cSC4EffectsManager::CreateVisualEffect(char const*, cISC4VisualEffect**)` at
  `0x00406E76`
- `cSC4EffectsManager::sCreateEffect(...)` at `0x004040B0`

Useful constructors / COM factories:

- `cSC4EffectsManager::cSC4EffectsManager()` at `0x00407E30`
- `GZCOM_CreatecSC4EffectsManager()` at `0x004084C8`
- `cSC4EffectsParser::cSC4EffectsParser()` at `0x00402E86`
- `cSC4EffectsResource::cSC4EffectsResource()` at `0x003DD7C0`

Useful CLSIDs:

- `GZCOM_cSC4EffectsManagerCLSID()` returns `0x49822F75`
- `GZCOM_cSC4EffectsParserCLSID()` returns `0xA956AD14`
- `GZCOM_cSC4EffectsResourceCLSID()` returns `0x2A5118AD`
- `GZCOM_cSC4EffectsResourceFactoryCLSID()` returns `0xEA5118B5`

## LoadAllEffects

Function:

- `cSC4EffectsManager::LoadAllEffects()`
- address: `0x00409E36`

Recovered behavior:

1. Read the framework setting `packedeffects`.
2. Lowercase the value and compare it against `off`.
3. Build `PluginDirectory() + "main.fx"`.
4. Call `BeginCollection()`.
5. If packed effects are enabled:
   - load resource key `T=0xEA5118B0, G=0xEA5118B1, I=1`
   - import that resource into the manager's embedded
     `cSC4EffectsCollection`
   - if plugin `main.fx` exists, point the file parser at it and parse it
   - enumerate all resources of type `0xEA5118B0`
   - import every resource whose instance is not `1`
6. If packed effects are disabled:
   - point the file parser at `DataDirectory() + "Effects/config.fx"`
   - also point it at plugin `main.fx`
   - parse both through the same parser pipeline
7. Call `EndCollection()`.

Notes:

- `main.fx` is only referenced once in this binary, from `LoadAllEffects()`.
- `Effects/config.fx` is also only referenced from `LoadAllEffects()`.
- The fallback path uses forward slashes in this build:
  `Effects/config.fx`.

## Init Path

Function:

- `cSC4EffectsManager::Init()`
- address: `0x0040A2DE`

Recovered behavior:

1. Guard against double-init.
2. Cache terrain extents, renderer, weather simulator, and a vertex-buffer
   extension interface.
3. Create flash materials.
4. Allocate and initialize `cSC4EffectMaps`.
5. Acquire a file parser COM object and a `cSC4EffectsParser` COM object.
6. Call `LoadAllEffects()`.
7. Register for a batch of message IDs through `spMS2`.
8. Register cheat commands through `spCCM`:
   - `Effect`
   - `Snow`
   - `HeapCheck`
9. Register a callback timer.
10. Add the node `effects` to the game-data registry.
11. Add the callable `create_effect` to the game-data registry.

This is the method that wires file loading, parser state, message handling, and
script exposure into one subsystem.

## Collection and Lookup Model

`cSC4EffectsManager` embeds a `cSC4EffectsCollection` and maintains additional
manager-side lookup state.

Important collection flow:

- `BeginCollection()` clears:
  - the effect-key hash map
  - the message-trigger hash map
  - the underlying `cSC4EffectsCollection` build state
- `EndCollection()`:
  - finalizes the collection
  - copies render-curve and global settings into manager state
  - rebuilds the effect-key hash map
  - rebuilds the message-trigger hash map
  - re-registers message-trigger subscriptions

The practical consequence is that file/resource parsing populates the collection
first, and only after `EndCollection()` do lookups and trigger-based spawning
become live.

## Runtime Creation Path

Name-based runtime creation goes through:

- `cSC4EffectsManager::HasVisualEffect()` at `0x00405476`
- `cSC4EffectsManager::CreateVisualEffect(char const*, ...)` at `0x00406E76`

Recovered behavior:

- effect names are normalized to lowercase before lookup
- the lookup goes through `cSC4EffectsCollection`
- if found, the manager allocates or reuses a `cSC4VisualEffect`
- the effect description is attached
- the effect can then be transformed and started

The script-facing wrapper is:

- `cSC4EffectsManager::sCreateEffect(...)` at `0x004040B0`

That wrapper:

1. resolves the `cSC4EffectsManager` instance from the game-data registry
2. reads command parameters:
   - effect name
   - position / transform inputs
3. calls the manager creation path
4. applies the transform
5. starts the effect
6. returns success as a variant

## Message-Driven Effects

`cSC4EffectsManager::DoMessage()` at `0x0040A90A` is a major part of the
subsystem's "magic".

Recovered behavior:

- it hashes incoming message IDs through a manager-side trigger map
- if a trigger entry matches, it spawns an effect and places it in world space
- it also handles normal lifecycle events such as:
  - animation tick
  - zoom / rotation changes
  - occupant inserted / removed
  - render property changes
  - periodic effects tick
  - cheat command dispatch

This means parsed effect definitions are not just data blobs for manual
creation; they are also bound into the game's message system and can fire
automatically from stock engine events.

## Hot Reload Behavior

Effects are not startup-only.

`EffectsTickCallback()` at `0x004042B0` posts message `0x697D01ED`, and
`cSC4EffectsManager::DoEffectsTick()` handles it.

Recovered behavior:

- check whether the file parser reports changes
- if so:
  - call `BeginCollection()`
  - call `ParseEffectsFiles()`
  - call `EndCollection()`
  - refresh active visual effects
  - notify the parser side that parsing completed

So at least part of the file-based effects pipeline is designed to be reloaded
while the manager is running.

## Known Effect-Related Names

Useful strings seen in the symbolized binary during this pass:

- `cSC4EffectsManager`
- `cSC4EffectsParser`
- `cSC4EffectsResource`
- `cSC4EffectsResourceFactory`
- `effects`
- `create_effect`
- `water_effects`
- `atmospheric_effects`
- `visualEffect`
- `particleEffect`
- `dynamicParticleEffect`
- `decalEffect`
- `effect`
- `effectGroup`
- `effectID`

These are strong next pivots for grammar recovery and data-layout work.

## Parser Grammar Skeleton

The parser root is:

- `cSC4EffectsParser::RegisterCommands()`
- address: `0x00402756`

Recovered top-level commands:

- `renderprop`
- `textureID`
- `modelID`
- `soundID`
- `brushID`
- `effectID`
- `effectGroup`
- `instance`
- `testEffect`
- `messageTrigger`
- `camera`
- `setPriority`
- `effectsResource`
- `loadResource`
- `effect`
- `particles`
- `dynamicParticle`
- `decal`
- `shake`
- `light`
- `sequence`

This establishes that `.fx` files are parsed as a command / block DSL rather
than as a binary blob or a simple key-value format.

## Effect Block Grammar

The core grouped effect block is handled by:

- `cGroupEffectCommand::RegisterCommands()` at `0x00782234`
- `cGroupEffectCommand::Parse(...)` at `0x0078510A`
- `cGroupEffectCommand::EndBlock(...)` at `0x007855E6`

Recovered nested commands inside an `effect` block:

- `particleEffect`
- `dynamicParticleEffect`
- `decalEffect`
- `shakeEffect`
- `flashEffect`
- `tintEffect`
- `soundEffect`
- `chainEffect`
- `brushEffect`
- `scrubberEffect`
- `demolishEffect`
- `gameEffect`
- `mapEffect`
- `automataEffect`
- `sequenceEffect`
- `cameraEffect`
- `visualEffect`
- `select`
- `particleSequence`

Recovered parse behavior for `effect`:

- the block takes a name as its first non-switch argument
- nested `effect` blocks are rejected with:
  `Can't nest effects! (Perhaps you meant visualEffect?)`
- supported switches include:
  - `viewRelative`
  - `noAutoStop`
  - `hardStop`
  - `rigid`
  - `noPropagate`
  - `applyCursor`
  - `ignoreOrientation`
  - `noLODStop`
  - `manualRestart`
- it also accepts:
  - `-startMessage <u32> <u32> <u32>`
  - `-priority <1..5>`

The end of the block commits the assembled `cSC4EffectDescription` into the
active collection.

## Named Definition Blocks

Several top-level definition blocks share the same basic parse pattern:

- require a name
- optionally accept inheritance as `: <baseName>`
- initialize a fresh description object
- optionally clone a base definition by name
- then parse nested subcommands

Recovered handlers:

- `cParticleEffectCommand::Parse(...)` at `0x0078B1A4`
- `cDynamicParticleEffectCommand::Parse(...)` at `0x0078A162`
- `cDecalEffectCommand::Parse(...)` at `0x00789A0A`
- `cShakeEffectCommand::Parse(...)` at `0x007894DA`
- `cLightEffectCommand::Parse(...)` at `0x0078925A`

Recovered common syntax shape:

```text
particles <name> [: <baseParticle>] { ... }
dynamicParticle <name> [: <baseDynamicParticle>] { ... }
decal <name> [: <baseDecal>] { ... }
shake <name> [: <baseShake>] { ... }
light <name> [: <baseLight>] { ... }
```

The parser throws `No name specified!` if the name is missing and
`Expecting ':'` if inheritance syntax is malformed.

## Nested Block Registrations

Recovered nested command sets for the named definition blocks:

### `particles`

Registered by `cParticleEffectCommand::RegisterCommands()` at `0x00782EB6`.

Nested commands:

- `zoom`
- `color`
- `alpha`
- `size`
- `aspect`
- `rotate`
- `source`
- `emit`
- `force`
- `warp`
- `randomWalk`
- `stretch`
- `life`
- `rate`
- `inject`
- `maintain`
- `texture`
- `model`
- `align`
- `collide`
- `terrainRepel`
- `effectBase`
- `timedEffect`
- aliases:
  - `color255`
  - `colour`
  - `colour255`
  - `alpha255`

### `dynamicParticle`

Registered by `cDynamicParticleEffectCommand::RegisterCommands()` at
`0x007834EA`.

Nested commands:

- `effectBase`
- `model`
- `mass`
- `friction`

### `decal`

Registered by `cDecalEffectCommand::RegisterCommands()` at `0x00783872`.

Nested commands:

- `color`
- `color255`
- `colour`
- `colour255`
- `alpha`
- `alpha255`
- `size`
- `aspect`
- `rotate`
- `life`
- `texture`

### `shake`

Registered by `cShakeEffectCommand::RegisterCommands()` at `0x00783C5C`.

Nested commands:

- `length`
- `amplitude`
- `frequency`
- `shakeAspect`
- `table`

Recovered defaults from `cShakeEffectCommand::Parse(...)`:

- length defaults to `5.0f`
- amplitude defaults to `1.0f`
- aspect defaults to `1.0f`
- default table vectors are seeded during parse

### `light`

Registered by `cLightEffectCommand::RegisterCommands()` at `0x00783E78`.

Nested commands:

- `strength`
- `length`
- `color`
- `color255`
- `colour`
- `colour255`

### `sequence`

Registered by `cSequenceEffectCommand::RegisterCommands()` at `0x00784058`.

Nested commands:

- `wait`
- `play`

## Effect ID and Group ID Syntax

Recovered simple parse handlers:

- `cEffectGroupIDCommand::RegisterCommands()` at `0x00781A66`
- `cEffectGroupIDCommand::Parse(...)` at `0x0077DDB0`

Recovered syntax:

```text
effectGroup <uint> {
    instance <...>
}
```

At minimum:

- `effectGroup` requires an ID
- missing ID throws `No ID specified!`
- the block registers an `instance` subcommand

There is also a top-level `effectID` command, but this pass did not yet recover
its full parse handler.

## Open Questions

- What is the exact grammar accepted by `cSC4EffectsParser` for blocks like
  `visualEffect`, `particleEffect`, `decalEffect`, `effectGroup`, and `effectID`?
- What is the precise binary layout of packed effects resources with type
  `0xEA5118B0`?
- How are message-trigger descriptions authored in the source `.fx` format?
- Which definitions in `main.fx` override stock packed resources versus append
  new entries?

## Suggested Next Pivots

- `cSC4EffectsParser` and its block handlers
- `cSC4EffectsCollection` insert / finalization paths
- packed effects resource serialization / deserialization
- the message-trigger description vector consumed by `EndCollection()` and
  `DoMessage()`
