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

## Top-Level Resource Name Maps

One important parser-side mechanism is a set of top-level commands that bind
symbolic names to numeric resource IDs. Nested effect-description commands then
look those names up through parser-owned maps.

Confirmed so far:

- `cTextureIDCommand::cTextureIDCommand()` at `0x00781846`
- `cTextureIDCommand::Parse(...)` in the Mac symbolized binary

Recovered `textureID` behavior:

- it requires exactly 3 tokens total:
  - `textureID`
  - `<name>`
  - `<uint>`
- otherwise it throws `Wrong number of arguments`
- it lowercases `<name>`
- it inserts or updates that key in the parser texture map at `+0xB4`
- it parses the third token with `nSCRes::ParseUint(...)`
- it stores the resulting numeric texture ID as the map value

So the authoring shape is:

```text
textureID my_texture_name 12345678
```

and nested commands such as `decal` `texture ...` later resolve the final
non-switch argument through that map.

Practical implication for decals:

```text
textureID scorch_tex 12345678

decal scorch_mark
    texture scorch_tex -draw decal
end
```

The `texture` line does not define the numeric ID itself. It only looks up the
symbolic name `scorch_tex`, lowercases it, and stores the resolved numeric
texture ID into the current decal description.

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
- the active effect name is stored in parser state at `+0x434`
- a fresh default `cSC4EffectDescription` is copied into parser state at
  `+0x43C` when the block opens
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
- after parsing its header, the block calls the generic block-command
  `StartBlock(...)` path, so the active `effect` block is pushed onto the
  parser block stack and later closed by a matching `end`

Additional recovered details:

- `-startMessage` writes three `uint32` values into parser state; this is
  distinct from top-level `messageTrigger`
- if the effect name exists in the parser's effect-remapping map, the remap
  entry overrides normal `-priority` parsing and marks the remap entry as used
- otherwise `-priority` is parsed normally and bounded to `1..5`

Recovered end-block behavior:

- `cGroupEffectCommand::EndBlock(...)` checks the active collection pointer at
  parser `+0x98`
- if present, it calls the collection vfunc at `+0x14` with:
  - the current effect name from parser `+0x434`
  - the assembled `cSC4EffectDescription` at parser `+0x43C`
- after commit, it clears the current effect name back to `""`

Interpretation:

- `effect <name> ... end` is not just a syntactic grouping node
- it opens a parser-side "current effect" record, accumulates nested child
  components into one `cSC4EffectDescription`, and commits that named effect to
  the active collection only when the matching `end` is processed

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

Mac symbolized anchors:

- constructor: `cDecalEffectCommand::cDecalEffectCommand()` at `0x0077D186`
- nested command class: `cGroupDecalCommand::cGroupDecalCommand()` at
  `0x00781DB0`
- parse: `cDecalEffectCommand::Parse(...)` at `0x00789A0A`
- end-block commit: `cDecalEffectCommand::EndBlock(...)` at `0x0078458E`

Recovered behavior:

- `decal <name>` requires a name; missing one throws `No name specified!`
- the parser stores the active decal name in parser state at `+0x368`
- it seeds a fresh current decal-description object in parser state at `+0x370`
- `decal <name> : <baseName>` inheritance is supported
- if a base name is provided and an active collection is present, the parser
  validates / copies from an existing decal-family definition before opening the
  block
- on `end`, the active decal name plus assembled description are committed into
  the active collection, then the current decal name is cleared back to empty

Recovered subcommand parser details:

- `cDecalColorCommand::Parse(...)` clears the current decal color curve at
  parser `+0x3A4`, parses each non-switch argument with `nSCRes::ParseColor(...)`,
  and appends each parsed color sample to that curve
- `color255` / `colour255` are handled by the same parser and then scale the
  finished color curve by `1 / 255`
- practical implication: each color sample is one argument passed to
  `ParseColor(...)`, so RGB triples need to be grouped as a single argument, for
  example `color "1.0 0.8 0.3"`
- `cDecalAlphaCommand::Parse(...)` clears the current alpha curve at parser
  `+0x398`, parses each non-switch argument as one float sample, and appends the
  results to that curve
- `alpha255` is handled by the same parser and scales the finished alpha curve
  by `1 / 255`
- `alpha` also supports `-vary <float>`; the value is parsed through
  `ParseRangedFloat(..., 0.0, 1.0)` and stored at parser `+0x3BC`
- `cDecalAspectCommand::Parse(...)` clears the current aspect curve at parser
  `+0x3B0`, parses each non-switch argument as one float sample, and appends the
  results to that curve
- `cDecalSizeCommand::Parse(...)` clears the current size curve at parser
  `+0x38C`, parses each non-switch argument as one float sample, and appends the
  results to that curve
- `size` also supports `-vary <float>`; the value is parsed through
  `ParseRangedFloat(..., 0.0, 1.0)` and stored at parser `+0x3C0`
- `size -cityScale` sets flag bit `4` in the decal-description bitset at parser
  `+0x370`
- `cDecalLifeCommand::Parse(...)` parses the first non-switch argument, if
  present, as the decal lifetime float and stores it at parser `+0x37C`
- `life` also supports these mode switches:
  - `-static`
  - `-loop`
  - `-single`
  - `-sustain`
- `-static` sets bit `6` in the decal-description flag bitset at parser
  `+0x370` and also sets the mode byte at `+0x379` to `1`
- `-loop` sets the mode byte at `+0x379` to `1`
- `-single` sets the mode byte at `+0x379` to `2`
- `-sustain` sets the mode byte at `+0x379` to `3`
- `cDecalRotateCommand::Parse(...)` clears the current rotation curve at parser
  `+0x380`, parses each non-switch argument as one float sample, and appends the
  results to that curve
- `rotate` also supports `-vary <float>`; the value is parsed through
  `ParseRangedFloat(..., 0.0, 1.0)` and stored at parser `+0x3C4`
- `cDecalTextureCommand::Parse(...)` controls both texture lookup and several
  rendering / UV flags
- `texture -draw <enum>` parses a draw mode through
  `ParseEnum(..., kDecalDrawTypes, ...)` and stores the result in the draw-mode
  byte at parser `+0x378`
- recovered `kDecalDrawTypes` mapping:
  - `decal` -> `0`
  - `additive` -> `1`
  - `modulate` -> `2`
  - `decalInvertDepth` -> `3`
  - `decalNoOverlap` -> `4`
- `texture -light` sets flag bit `1` in the decal-description bitset at
  parser `+0x370`
- `texture -water` sets flag bit `2`
- `texture -repeat <float>` sets flag bit `3` and stores the repeat value at
  parser `+0x3C8`
- `texture -cityScale` belongs to `size`, not `texture`
- `texture -ring` sets flag bit `5`
- `texture -offset <vec2>` parses a 2D vector and stores it at parser
  `+0x3CC/+0x3D0`
- after switch handling, `texture` resolves a texture name through the parser's
  texture-id map at `+0xB4`; the lookup name is lowercased first
- if the texture name is not found, it throws `No such texture: '%s'`
- on success, the resolved texture id is stored at parser `+0x374`

Still unresolved from the Mac side:

- the concrete enum values behind `kDecalDrawTypes`

Note on one decompilation artifact:

- the recovered parse currently decompiles one inherited-base failure
  path as `Unknown particle definition: %s`
- that is almost certainly a recovered-string / slot-label mismatch rather than
  the true semantic, because the surrounding parser state and collection slots
  clearly line up with the top-level `decal` family rather than `particles`

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

Recovered usage connection:

- named `light` definitions are later referenced by both `flashEffect` and
  `tintEffect`
- at runtime those references resolve through the collection's light-definition
  table (`AddLightDescription(...)` / `GetLightDescription(...)`)

Recovered parser detail:

- `cLightStrengthCommand::Parse(...)` at `0x00788C86` clears parser strength
  curve storage at `+0x420`, then parses each non-switch argument as one float
  sample via `nSCRes::ParseFloat(...)`
- `cLightColorCommand::Parse(...)` at `0x00789148` clears parser curve storage
  at `+0x414`, then parses each non-switch argument as one color sample
- each sample is parsed through `nSCRes::ParseColor(...)` at `0x0041D5E4`
- `ParseColor(...)` requires exactly three color components per sample and
  throws `Wrong number of color components` otherwise
- this means `light` `strength` expects one or more plain scalar arguments
- this means `light` `color`/`colour` expects one or more color samples, where
  each sample must remain a single argument string
- `color255` / `colour255` use the same parser, then scale all samples by
  `1/255`

Practical syntax implication:

```text
strength 1.0
strength 0.0 1.0 0.0
color "1.0 1.0 1.0"
color "1.0 1.0 1.0" "0.2 0.2 0.4"
color255 "255 255 255"
```

Unquoted forms like this are parsed incorrectly:

```text
color 1.0 1.0 1.0
```

because each `1.0` becomes its own argument and then fails `ParseColor(...)`.

### `sequence`

Registered by `cSequenceEffectCommand::RegisterCommands()` at `0x00784058`.

Top-level parse handler:

- `cSequenceEffectCommand::Parse(...)` at `0x00784724`

Nested commands:

- `wait`
- `play`

Recovered top-level switches:

- `-loop`
- `-noOverlap`
- `-hardStart`

Recovered semantics:

- `wait <a>` waits exactly `a` seconds before advancing
- `wait <a> <b>` waits a random time uniformly in `[a, b]`
- `play <effectName> <a>` starts the named visual effect immediately, then
  advances after `a` seconds
- `play <effectName> <a> <b>` starts the named visual effect immediately, then
  advances after a random delay in `[a, b]`
- `-loop` restarts the sequence from item `0` after the last entry
- `-noOverlap` sets the runtime flag that prevents the previous played child
  effect from overlapping into the next step
- `-hardStart` sets the runtime flag that makes played child effects start with
  a hard transition

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

## Resource Import / Include-Like Commands

Two top-level commands are the closest thing this DSL has to include logic:

- `loadResource`
- `effectsResource`

### `loadResource`

Handler:

- `cLoadResourceCommand::Parse(...)` at `0x00780820`

Recovered syntax shape:

```text
loadResource <uint>
```

Recovered behavior:

- parses one non-switch argument as a `uint`
- looks up a packed effects resource with fixed type/group:
  - `T = 0xEA5118B0`
  - `G = 0xEA5118B1`
  - `I = <parsed uint>`
- if the resource is not found, parsing throws:
  `Effects resource 0x%08x not found`
- if found, it imports that resource into the active effects collection

Recovered resource-manager path:

- `loadResource` goes through
  `cGZPersistResourceManager::GetResource(...)` at `0x00329F8E`

Interpretation:

- `loadResource` is a packed-resource import mechanism
- it does **not** load another text `.fx` file by filename

### `effectsResource`

Handlers:

- `cEffectsResourceCommand::Parse(...)` at `0x00788D62`
- `cEffectsResourceCommand::EndBlock(...)` at `0x00788FA6`

Recovered syntax shape:

```text
effectsResource <uint> <name>
    ...
end
```

Recovered behavior:

- requires exactly two arguments after the command
- cannot be nested; nested use throws:
  `Can't nest resource blocks`
- the first argument is parsed as a `uint` instance id
- the parser opens / creates a packed effects resource with fixed type/group:
  - `T = 0xEA5118B0`
  - `G = 0xEA5118B1`
  - `I = <parsed uint>`
- the second argument is passed into the resource object before the block body
  is parsed
- while inside the block, the active collection target is swapped to that
  resource-backed collection
- on block end, the resource is saved back out; failures throw:
  - `couldn't find segment for effect group 0x%08x`
  - `couldn't save effects resource key 0x%08x`

Important parser gate:

- `cSC4EffectsParser::EnableResourceSaving()` at `0x004017B2` does exactly one
  thing: sets parser byte flag `+0xAC = 1`
- `cEffectsResourceCommand::Parse(...)` only creates / binds the backing
  `cISC4EffectsResource` if that same parser flag is already set
- `cEffectsResourceCommand::EndBlock(...)` immediately returns if either:
  - parser byte flag `+0xAC == 0`
  - active effects-resource pointer `+0x9C == 0`
- this means an `effectsResource ... end` block can parse as a harmless no-op
  container if resource saving was never enabled on the parser instance

Recovered resource-manager path:

- `effectsResource` creation goes through
  `cGZPersistResourceManager::GetNewResource(...)` at `0x0032A102`
- on block end it resolves a DB segment via
  `cGZPersistResourceManager::FindDBSegment(...)` at `0x0032B030`
- it then saves via
  `cGZPersistResourceManager::Save(...)` at `0x003293A2`, which calls
  `cGZPersistResourceManager::SaveResource(...)` at `0x0032941C`
- the segment lookup key used at block end is the resource **group** id,
  which for effects resources is `0xEA5118B1`

Relevant loader context:

- `cSC4EffectsManager::LoadAllEffects()` calls
  `cSC4EffectsParser::SetManager(...)` through the parser interface
- in the normal retail startup path recovered so far, I do **not** see
  `EnableResourceSaving()` being called before `main.fx` is parsed
- the wrapper `nSCRes::cFileParser` mode argument used by `AddInputFilePath`
  does not currently show any save-enable handoff either; recovered bits are:
  - bit `0x2`: attach directory watcher / hot-reload support
  - bit `0x1`: stored in the input-file spec, but its exact meaning is still
    unresolved

Windows PE confirmation:

- the Windows retail binary has the same gate, but with shifted parser offsets:
  - save-enable flag at parser `+0xA4`
  - active effects-resource pointer at parser `+0x94`
  - current packed key fields at parser `+0x98`, `+0x9C`, `+0xA0`
- `FUN_00596810` is the PE32 `effectsResource` parse handler:
  - it throws `Can't nest resource blocks`
  - it only calls `GetNewResource(...)` if parser byte `+0xA4` is already set
- `FUN_00596970` is the PE32 end-block handler:
  - it immediately returns unless parser byte `+0xA4 != 0` and parser
    resource pointer `+0x94 != 0`
  - if it does proceed, it calls the same segment lookup and save path as the
    Mac binary
- `FUN_005945B0` is the PE32 effects load function containing the
  `packedeffects` / `main.fx` bootstrap
- in that Windows load path, the parser is connected and `main.fx` is parsed,
  but I do **not** see a call that would obviously enable resource saving first

Interpretation:

- `effectsResource` is an authoring / serialization block for packed effects
  resources
- it also does **not** appear to include another text `.fx` file by path
- the save path is real code, not just parser scaffolding
- however, it only works if the resource manager currently has a writable DB
  segment registered with segment id `0xEA5118B1`
- and the effects parser must first have resource saving explicitly enabled via
  `EnableResourceSaving()`

Current conclusion:

- the `.fx` DSL does have resource import logic
- the recovered include-like path is for packed SC4 effects resources, not for
  arbitrary extra `.fx` text files
- based on the loader code recovered so far, the direct text-file entry points
  are still just stock `Effects/config.fx` and plugin `main.fx`
- the game does have a real packed-effects save path
- but the normal retail `LoadAllEffects()` path does not currently show the
  required `EnableResourceSaving()` call, so `effectsResource` inside
  `main.fx` is very likely non-persistent unless some other hidden setup path
  flips that parser flag first

### Generic Block Dispatch

The generic parser-side `end` flow is now clear in the Mac binary:

- `nSCRes::cEndBlockCommand::Parse(...)` at `0x007763F2`
- `nSCRes::cBlockCommand::StartBlock(...)` at `0x003F849C`
- `nSCRes::cBlockCommandT<nSCRes::cCommandParser>::Parse(...)` at
  `0x00775E4C`
- `nSCRes::cBlockCommandT<nSCRes::cCommandParser>::EndBlock(...)` at
  `0x00775E80`

Recovered behavior:

- block commands call `StartBlock(...)` to push themselves onto the parser's
  block stack at parser offset `+0x30`
- `cEffectsResourceCommand::Parse(...)` at `0x00788D62` ends by calling the
  block-command `StartBlock(...)` vfunc, so an `effectsResource` block really
  does register itself on that stack
- when the parser later sees `end`, `cEndBlockCommand::Parse(...)`:
  - checks that the block stack is not empty
  - fetches the top block via `vector<...>::back()`
  - converts the generic parser interface back to the concrete parser pointer
    via parser vfunc `+0x34`
  - calls the popped block's end-block vfunc
  - then pops the stack entry by decrementing the vector end pointer

For `effectsResource`, that popped-block end callback is:

- `(anonymous namespace)::cEffectsResourceCommand::EndBlock(...)` at
  `0x00788FA6`

Interpretation:

- on Mac, the expected commit path is:
  `effectsResource::Parse -> StartBlock push -> "end" -> cEndBlockCommand::Parse -> cEffectsResourceCommand::EndBlock`
- so if the Windows retail path logs `effectsResource::Parse` but never reaches
  the matching `EndBlock`, the likely failure is no longer in resource-manager
  config alone
- the most likely remaining causes are:
  - the `end` token is not being routed through the generic `cEndBlockCommand`
    path in that runtime
  - or the active `effectsResource` block is not actually present on the parser
    block stack by the time `end` is parsed

## Manual Execution Via Cheat

The practical runtime test hook is:

- `cSC4EffectsManager::DoEffectCheat(cRZCmdLine&)` at `0x00409012`

Recovered special subcommands:

- `kill`
- `save`
- `load`
- `showMaps`
- `clearMaps`
- `prop`

Recovered string addresses used by the handler:

- `0x005E0964`: `kill`
- `0x005D027C`: `save`
- `0x005E096C`: `load`
- `0x005E0974`: `showMaps`
- `0x005E0980`: `clearMaps`
- `0x005E098C`: `softStop`
- `0x005D8B78`: `prop`
- `0x005E00E8`: `No such effect`

### Slot Model

`DoEffectCheat()` maintains a vector of test effects at manager offset
`+0xDC4`.

Recovered behavior:

- if the first token is numeric, it is parsed as a slot index
- if no numeric token is given, slot `0` is used
- starting a new effect in an occupied slot first stops the previous effect
- if the next token is `softStop`, the existing effect is stopped softly
- otherwise the stop is hard

This yields the following practical forms:

```text
Effect <slot> softStop
Effect <slot> <effectName> ...
Effect <effectName> ...
```

### Visual Effect Creation Forms

After slot handling, the cheat calls:

- `HasVisualEffect()`
- `CreateVisualEffect()`
- then starts the created effect object

Recovered argument forms:

```text
Effect <effectName>
Effect <effectName> <surfaceX> <surfaceZ>
Effect <effectName> <worldX> <worldY> <worldZ>
```

Recovered placement behavior:

- with only `<effectName>`, the effect is placed at the current cursor /
  terrain point
- with `<surfaceX> <surfaceZ>`, those map-space coordinates are converted
  through `SurfacePointTo3D(...)`
- with `<worldX> <worldY> <worldZ>`, the transform is filled directly from the
  three floats
- the cursor/surface forms add `+4.0f` to Y before applying the transform

The created effect is then started through the visual-effect vtable.

### Alternate Four-Value Path

If four numeric values follow the effect name, `DoEffectCheat()` does not build
the normal transform. Instead it:

- parses four numeric values
- truncates them to integers
- stores them as four floats
- calls the effect vtable at `+0x30` with selector `0x0C` and count `4`
- then starts the effect

Recovered syntax shape:

```text
Effect <effectName> <a> <b> <c> <d>
```

The exact semantic meaning of selector `0x0C` is still unresolved. It is
probably a parameter or payload-setting path rather than ordinary placement.

### `prop` Test Path

The `prop` subcommand is separate from normal visual-effect spawning.

Recovered behavior:

- `Effect prop` defaults the prop name to `pothole`
- `Effect prop <name>` uses the supplied prop exemplar / identifier
- with no coordinates, it uses the current cursor / terrain point
- with two coordinates, it converts them through `SurfacePointTo3D(...)`
- it then assigns the prop name, sets the position, inserts the occupant into
  the occupant manager, and starts it

### Most Likely Minimal Workflow

For a custom entry added to `main.fx`, the shortest likely proof path is:

```text
Effect MyEffect
```

or, if the effect wants explicit placement:

```text
Effect MyEffect 128 128
Effect MyEffect 10 20 30
```

If repeated tests are needed without leaking prior instances, the slot form is
the intended harness:

```text
Effect 0 MyEffect
Effect 0 softStop
```

## Open Questions

- What is the exact grammar accepted by `cSC4EffectsParser` for blocks like
  `visualEffect`, `particleEffect`, `decalEffect`, `effectGroup`, and `effectID`?
- What is the precise binary layout of packed effects resources with type
  `0xEA5118B0`?
- How are message-trigger descriptions authored in the source `.fx` format?
- Which definitions in `main.fx` override stock packed resources versus append
  new entries?
- What does the visual-effect vtable selector `0x0C` represent in the
  four-value cheat path?

## Suggested Next Pivots

- `cSC4EffectsParser` and its block handlers
- `cSC4EffectsCollection` insert / finalization paths
- packed effects resource serialization / deserialization
- the message-trigger description vector consumed by `EndCollection()` and
  `DoMessage()`
- exact handler meanings for `visualEffect`, `soundEffect`, `messageTrigger`,
  and `testEffect`

## Timbuktu Presentation Cross-Check

The local presentation `docs/TimbuktuEffects.html` strongly validates the
reverse-engineered parser model.

High-signal statements from the presentation:

- the system uses a "Very simple command-based scripting system"
- the generic syntax is:

```text
command arg1 arg2 -switch1 -switch2 switch2arg

blockCommand
    ...
end
```

- inheritance is explicit and uses `:`

Example shown in the presentation:

```text
particles smokeDark : smoke
    color (0.2, 0.2, 0.3) (0.1, 0.1, 0.1)
    alpha 0.1 0.2 0.2 0.1 0
end
```

- scripts are parsed into description blocks; there is "no live script logic"
- each component has its own class, driven by a description, with associated
  parser commands
- the effects manager controls:
  - instantiation
  - script reload
  - LOD changes
  - parameters
- a visual effect is a collection of component effects
- visual effects can contain particles, decals, framebuffer effects, sequences,
  and other components
- visual effects are also components, so nesting is supported conceptually
- game-specific components include:
  - terrain brushes
  - automata attractors
  - pause / unpause simulator
  - camera manipulation
  - pool water
  - game messages
- hard and soft transitions are first-class concepts
- LOD and priority scaling are built into the authored effect system

The presentation also includes code that starts an effect by calling
`CreateVisualEffect("nhood_fly", ...)`, setting a transform, then calling
`Start()`. That lines up directly with the `cSC4EffectsManager::CreateVisualEffect`
and script wrapper paths recovered from the binary.

### Strong Matches Between Presentation and Binary

These presentation concepts match the reverse-engineered Mac binary almost
exactly:

- `effect` as a composite visual effect
- component sub-effects like particles, decals, camera, brush, sequence, and
  attractor-like behaviors
- command-based block parsing
- inheritance syntax using `:`
- `CreateVisualEffect(...)` as the primary runtime entry point
- hard / soft stop semantics
- LOD and priority support
- manager-controlled script reload

### Likely Interpretation

The presentation makes the current best interpretation much stronger:

- top-level blocks like `particles`, `dynamicParticle`, `decal`, `shake`, and
  `light` are reusable component descriptions
- `effect` defines a composite visual effect that instantiates or references
  those components
- nested commands like `particleEffect`, `decalEffect`, `soundEffect`,
  `cameraEffect`, `brushEffect`, and `automataEffect` are component instances
  inside a composite effect
- `messageTrigger` and `startMessage` are not SC4-specific hacks; they fit the
  intended original system design

### One Important Difference

The presentation says visual effects can nest, while the SC4 parser throws:

- `Can't nest effects! (Perhaps you meant visualEffect?)`

The most likely explanation is that SC4's authored script distinguishes between:

- the top-level composite definition block: `effect`
- a nested component/reference command inside that block: `visualEffect`

So nesting exists, but not by literally putting an `effect` block inside another
`effect` block.

## Recovered Child Command Semantics

Three high-value parser handlers are now recovered well enough to author simple
 test content.

### `visualEffect`

Handler:

- `cGroupGroupCommand::Parse(...)` at `0x0078B656`

Recovered syntax shape:

```text
effect WrapperName
    visualEffect ExistingEffectName [shared desc-rec switches...]
end
```

Recovered behavior:

- valid only inside an `effect` block
- takes one non-switch argument: the referenced visual-effect name
- validates that the referenced effect already exists in the active collection
- throws:
  - `Not in effects block`
  - `Unknown visual effect: %s`
- emits a `cSC4EffectDescription::cDescriptionRec` with type `2`
- runs `ParseDescRecOptions(...)` on the same argument list
- appends the description record to the current effect's child list

This strongly confirms that `visualEffect` is a nested reference / instance of
another visual effect, not a nested top-level `effect` definition.

### `particleEffect`

Handler:

- `cGroupParticlesCommand::Parse(...)` at `0x0078D6C4`

Recovered syntax shape:

```text
effect WrapperName
    particleEffect ExistingParticleName [shared desc-rec switches...]
    particleEffect ExistingParticleName -shells <count> [float]
end
```

Recovered behavior:

- valid only inside an `effect` block
- takes one non-switch argument: the referenced named particle definition
- if an active collection is present, it validates the referenced name through
  the collection and throws:
  - `Not in effects block`
  - `Unknown particle definition: %s`
- emits a `cSC4EffectDescription::cDescriptionRec` with type `0`
- copies parser state at `+0x488` into the child desc-rec before option parsing
- copies parser flag `+0x494` into child desc-rec flag bit `1`
- runs `ParseDescRecOptions(...)` on the same argument list
- appends the finished description record to the current effect's child list at
  parser `+0x444`

Recovered `-shells` switch:

- syntax: `-shells <uint> [float]`
- the first value is parsed as an unsigned integer and stored in the desc-rec
- if a second value is present, it is parsed as a float and rounded to an
  integer before storage
- the exact runtime semantic of the second field is still unresolved, but the
  authoring shape is binary-backed

Interpretation:

- `particleEffect` is the nested reference / instance form for top-level named
  `particles` definitions
- unlike `visualEffect`, it carries extra particle-specific fields, including
  the `particleSequence` chain flag and optional `-shells` data

### `dynamicParticleEffect`

Handler:

- `cGroupDynamicParticleCommand::Parse(...)` at `0x00784148`

Recovered behavior:

- valid only inside an `effect` block
- emits a `cSC4EffectDescription::cDescriptionRec` with type `0x10`
- copies parser state at `+0x488` into the child desc-rec
- copies parser flag `+0x494` into child desc-rec flag bit `1`
- runs `ParseDescRecOptions(...)`
- appends directly to the current effect child list

Notable difference from `particleEffect`:

- this handler does not do the same active-collection name validation step
  before emitting the child desc-rec

### `decalEffect`

Mac symbolized anchor:

- child command class: `cGroupDecalCommand::cGroupDecalCommand()` at
  `0x00781DB0`

Windows PE cross-check:

- parser thunk / handler recovered around `0x005A04A4` from the
  `Unknown decal definition: %s` xref

Recovered behavior:

- `decalEffect` is only valid inside an `effect` block
- it takes one non-switch argument: the referenced named top-level `decal`
  definition
- if an active collection is present, it validates the referenced name and
  throws:
  - `Not in effects block`
  - `Unknown decal definition: %s`
- it emits a child `cSC4EffectDescription::cDescriptionRec` that later maps to
  runtime child type `1` (`decal effect`)
- it stores the referenced decal name into the emitted child record
- it then applies the shared description-record switches via
  `ParseDescRecOptions(...)`
- the finished child record is appended to the current effect's child list

Interpretation:

- `decalEffect` is the nested reference / instance form for top-level named
  `decal` definitions, analogous to `particleEffect` for `particles`
- no dedicated decal-only child-instance switches have been recovered yet beyond
  the shared desc-rec options

### Shared Description-Record Switches

`visualEffect` and sibling child commands use:

- `cSC4EffectsParser::ParseDescRecOptions(...)` at `0x00401D2C`

Recovered supported switches:

- `-offset <vec3>`
- `-rotateX <float>`
- `-rotateY <float>`
- `-rotateZ <float>`
- `-rotateXYZ <x> <y> <z>`
- `-rotateZXY <z> <x> <y>`
- `-scale <float>`
- `-lod <uint>`
- `-lodRange <min> <max>`
- `-emitScale <min> [max]`
- `-sizeScale <min> [max]`
- `-ignoreLength`
- `-respectLength`

Select-only support recovered from the same routine:

- `-prob <float>`

Restrictions recovered:

- `-prob` is only valid inside a `select` group
- `-lod` / `-lodRange` are rejected inside a `select` group with:
  `Can't specify LOD in a select`

### `select`

Handlers:

- `cGroupSelectCommand::Parse(...)` at `0x0078B4B8`
- `cGroupSelectCommand::EndBlock(...)` at `0x0077DA24`

Recovered behavior:

- `select` is a nested block inside an `effect`
- child entries inside the block use the shared description-record switches,
  including `-prob <float>`
- on block close, children without an explicit probability are assigned an even
  share of the remaining total weight
- the implementation normalizes weights into a 16-bit fixed-point total of
  `65536`
- the last child absorbs any leftover remainder from rounding

Interpretation:

- `select` is a weighted random-choice group for child effect components

### `particleSequence`

Handlers:

- `cGroupSystemSequenceCommand::Parse(...)` at `0x0078B43A`
- `cGroupSystemSequenceCommand::EndBlock(...)` at `0x0077CF7C`

Recovered behavior:

- `particleSequence` is a nested block inside an `effect`
- unlike top-level `sequence`, it does not allocate a separate sequence
  description object
- entering the block sets parser flag `+0x494` to `1`
- leaving the block clears that flag back to `0`
- `cGroupParticlesCommand::Parse(...)` at `0x0078D6C4` reads that flag and
  copies it into bit `1` of the emitted child description-record flags

What this means so far:

- `particleSequence` is a mode wrapper that changes how nested `particleEffect`
  entries are tagged, not a `wait` / `play` timeline like top-level `sequence`
- runtime meaning is now partly recovered:
  `cSC4EffectsManager::CreateComponentFromDescription(...)` at `0x00406246`
  tests that extra flag bit when instantiating model-based particle children
- when consecutive model-based particle components are tagged this way, the
  manager links them with
  `cSC4ModelBasedParticlesEffect::SetChainSystem(...)` at `0x0040FF74`
  and suppresses emission on the handed-off successor
- this means `particleSequence` is effectively a chaining / handoff mode for
  nested model-based `particleEffect` components, not a timed authoring
  sequence

### `soundEffect`

Handler:

- `cGroupSoundCommand::Parse(...)` at `0x0078CC8A`

Recovered syntax shape:

```text
soundEffect
    -name <soundName>
    -locationUpdateRate <float>
    -length <float>
```

Recovered behavior:

- `soundEffect` is anonymous here; if you provide non-switch arguments the
  parser throws `sound description unimplemented`
- `-name` is required and missing it throws:
  `Need at least -name for anonymous soundEffect`
- sound names are lowercased before lookup against the parser's sound-ID map
- unknown names throw `Unknown sound %s`
- `-locationUpdateRate <x>` stores `1.0 / x` when `x > 0`
- `-length <float>` sets an explicit sound-effect length

### `cameraEffect`

Handler:

- `cGroupCameraCommand::Parse(...)` at `0x0078B6B8`

Recovered supported switches:

- `-slave`
- `-target`
- `-attachRadius <float>`
- `-zoom <1..5>`
- `-rotation <0..3>`

Recovered behavior:

- `cameraEffect` is anonymous in this form
- `-zoom` stores a zero-based zoom level (`1..5` authoring becomes `0..4`
  internally)
- `-rotation` stores a bounded camera-rotation selector in range `0..3`

### `flashEffect`

Handler:

- `cGroupFlashCommand::Parse(...)` at `0x0078D176`

Recovered syntax shape:

```text
flashEffect <lightName>
flashEffect <lightName> -epicentre [radius]
flashEffect <lightName> -epicenter [radius]
```

Recovered behavior:

- `flashEffect` is only valid inside an `effect` block; otherwise it throws:
  `Not in effects block`
- it requires one non-switch argument naming an existing `light` definition
- unknown names throw:
  `unknown light definition: %s`
- on success it appends a `cSC4EffectDescription::cEventRec` to the current
  effect and marks it as a flash event
- the referenced `light` definition name is stored in that event record
- `-epicentre` / `-epicenter` mark the flash as localized around the effect's
  transformed position
- if a radius is supplied with `-epicentre`, it is parsed as a float
- if no radius is supplied, the localized flash radius defaults to `1000.0`

Runtime tie-in:

- `cSC4EffectsManager::StartAncilliary(...)` at `0x00405030` resolves the named
  `light` definition and calls `StartScreenFlash(...)`
- localized flashes use the effect's transformed position plus the stored
  epicentre radius
- non-localized flashes are full-screen / global flashes with no spatial falloff

### `tintEffect`

Handler:

- `cGroupTintCommand::Parse(...)` at `0x0078D014`

Recovered syntax shape:

```text
tintEffect <lightName>
```

Recovered behavior:

- `tintEffect` is only valid inside an `effect` block; otherwise it throws:
  `Not in effects block`
- it requires one non-switch argument naming an existing `light` definition
- unknown names throw:
  `unknown light definition: %s`
- on success it appends a `cSC4EffectDescription::cEventRec` to the current
  effect and marks it as a tint event
- unlike `flashEffect`, no extra switches were recovered in this parser

Runtime tie-in:

- `cSC4EffectsManager::StartAncilliary(...)` resolves the named `light`
  definition and calls `StartLightingTint(...)`
- `UpdateLightingTint(...)` then evaluates the same underlying light
  description as a time-varying lighting tint curve rather than a screen flash

### `chainEffect`

Handler:

- `cGroupChainCommand::Parse(...)` at `0x0078CBD4`

Recovered behavior:

- requires one non-switch argument
- stores that referenced name into parser field `+0x468`

Interpretation:

- this appears to be a lightweight chain / follow-up effect reference rather
  than a fully independent anonymous description block
- the exact runtime consumer of the parser field written by `chainEffect` is
  still not fully pinned down

## Runtime Component Instantiation

The key runtime bridge from parsed descriptions to live component effects is:

- `cSC4EffectsManager::CreateComponentFromDescription(...)` at `0x00406246`
- `cSC4EffectsManager::UpdateVisualEffect(...)` at `0x0040658E`

Recovered behavior:

- `UpdateVisualEffect(...)` walks the active
  `cSC4EffectDescription::cDescriptionRec` list for a visual effect and decides
  which child records to instantiate based on LOD, select-group state, and
  reload conditions
- `CreateComponentFromDescription(...)` then instantiates the concrete runtime
  component for each surviving child record and stores it into the owning
  `cSC4VisualEffect::cComponentRec`

Recovered child-type mapping from `CreateComponentFromDescription(...)`:

- type `0`: particle effect
- type `1`: decal effect
- type `2`: nested visual effect
- type `0x10`: dynamic particle effect
- types `3..15`: generic component-description families looked up through
  `cSC4EffectsCollection::cCompDescsRec`

### Runtime `select` Behavior

Runtime handlers:

- `cSC4EffectsManager::UpdateVisualEffect(...)` at `0x0040658E`

Recovered behavior:

- child description records with select-group id `0` are treated normally and
  can all instantiate
- child records with a nonzero select-group id are processed as one weighted
  choice group
- when the group id changes, the runtime rolls a fresh uniform random in
  `0..65535`
- each child adds its normalized weight to a cumulative total
- only the child whose cumulative total crosses the random threshold gets
  instantiated; the others in that select group are skipped

This confirms that `select` is not just a parser-side weighting construct. It
is an actual runtime random branch over component instantiation.

### Runtime `particleSequence` / Chain Behavior

Runtime handlers:

- `cSC4EffectsManager::CreateComponentFromDescription(...)` at `0x00406246`
- `cSC4ModelBasedParticlesEffect::SetChainSystem(...)` at `0x0040FF74`
- `cSC4ModelBasedParticlesEffect::HandOffParticleToChainSystem(...)` at
  `0x004102B6`
- `cSC4ModelBasedParticlesEffect::DestroyParticle(...)` at `0x004121DE`

Recovered behavior:

- the extra desc-rec flag emitted by `particleSequence` is consumed only for
  model-based particle children
- the manager carries a temporary "previous chained system" pointer while
  building child components
- when the next tagged model-based particle child is created, the previous
  system's `SetChainSystem(...)` is pointed at the new one
- the new successor system then has emission suppressed until particles are
  handed off into it
- when a particle reaches the handoff path,
  `HandOffParticleToChainSystem(...)` transfers state into the chained system
  and reinitializes the particle there

This means `particleSequence` is best understood as a particle-system relay:
successive model-based particle effects form a chain, and live particles can be
handed from one system to the next instead of simply dying.

### Model-Based Particle Sibling Effects

Runtime handlers:

- `cSC4ModelBasedParticlesEffect::SetDescription(...)` at `0x00411D1A`
- `cSC4ModelBasedParticlesEffect::CreateParticle(...)` at `0x0041204A`
- `cSC4ModelBasedParticlesEffect::HandOffParticleToChainSystem(...)` at
  `0x004102B6`
- `cSC4ModelBasedParticlesEffect::DestroyParticle(...)` at `0x004121DE`

Recovered behavior:

- model-based particle descriptions automatically probe for sibling visual
  effects named:
  - ``%s_slave``
  - ``%s_death``
- `CreateParticle(...)` can create the `%s_slave` visual effect and attach it
  to the live particle
- `HandOffParticleToChainSystem(...)` also creates / rebinds that `%s_slave`
  effect during particle handoff into the next chained system
- `DestroyParticle(...)` can create the `%s_death` visual effect when a
  particle is actually destroyed instead of handed off

Interpretation:

- model-based particle systems are designed to cooperate with sibling
  visual-effect definitions that represent per-particle slave visuals and
  destruction/death visuals
- the exact authoring switches that enable all of these sibling-effect paths
  are not yet fully named from the parser side, but the runtime suffix lookup
  is now explicit

### Base Particle Sibling Effects

Runtime handler:

- `cSC4ParticlesEffect::SetDescription(...)` at `0x00415CB8`

Recovered behavior:

- some particle descriptions also auto-probe for sibling effect names:
  - ``%s_terrain``
  - ``%s_water``
- when enabled, those sibling lookups are resolved during particle-description
  setup and stored on the particle effect object

Interpretation:

- particle definitions can have environment-specific sibling effects for terrain
  and water interaction, separate from the model-based `%s_slave` /
  `%s_death` paths

### `brushEffect`

Handler:

- `cGroupBrushCommand::Parse(...)` at `0x0078C690`

Recovered supported switches:

- `-name <brushName>`
- `-rate <float>`
- `-apply <float>`
- `-length <float>`
- `-zoom <1..5>`
- `-strength <min> [max]`
- `-width <min> [max]`
- `-level <float>`

Recovered behavior:

- missing `-name` throws:
  `need at least -name for an anonymous brush effect`
- brush lookup is case-insensitive
- unknown names throw `No such brush: '%s'`
- `-apply` writes the same numeric field as `-rate` and also sets an internal
  apply-mode flag
- `-zoom` is stored zero-based (`1..5` becomes `0..4`)

### `scrubberEffect`

Handler:

- `cGroupScrubberCommand::Parse(...)` at `0x0078BC26`

Recovered supported switches:

- `-demolish <float>`
- `-burn <float>`
- `-toxic <float>`
- `-extinguishFire <uint>`
- `-pauseSim [float]`
- `-pauseSimHidden [float]`
- `-pauseClock [float]`
- `-message <uint> [uint]`
- `-blob <1..8> <float> <float> [float]`
- `-rect <1..8> <float> <float> <float> [float]`
- `-noNetworks`
- `-noFlora`
- `-dezone`
- `-single`
- `-explode`
- `-createRubble`
- `-createBurntRubble`
- `-demolishEffectID <uint>`
- `-minDemolishSize <float>`
- `-maxDemolishSize <float>`

Recovered behavior:

- if none of the effect-shaping switches are present, parsing throws:
  `Need some options for anonymous scrubber effect`
- `-blob` / `-rect` both take a bounded first argument in range `1..8`
- pause-related switches optionally take a duration float
- demolition-related switches pack several mode bits and an effect-ID byte into
  one stored field

Important note:

- `demolishEffect`, `gameEffect`, and `mapEffect` are all registered using this
  same parser implementation, so they likely share most or all of this switch
  surface

### `automataEffect`

Handler:

- `cGroupAttractorCommand::Parse(...)` at `0x0078BA44`

Recovered supported switches:

- `-name <string>`
- `-group <string>`

Recovered behavior:

- at least one of `-name` or `-group` is required
- missing both throws:
  `Need at least -name or -group for anonymous attractor effect`
- `-group` sets an internal group-mode flag

### `testEffect`

Handler:

- `cTestEffectCommand::Parse(...)` at `0x00781150`
- later started by `cSC4EffectsParser::StartTestEffects()` at `0x00401842`

Recovered syntax shape:

```text
testEffect ExistingEffectName [pos2_or_pos3]
    -sourceScale <float>
    -scale <float>
    -trans <vec3>
    -speed <float>
    -target <vec3>
    -hard
```

Recovered behavior:

- creates a visual effect immediately during parse/load time
- the effect name must already exist, or parsing throws `No such effect`
- optional second non-switch argument is placement:
  - if omitted: default position is `(512, 100, 512)`
  - if it contains 3 values: parsed as a full `vec3`
  - otherwise: parsed as a `vec2`, terrain height is queried, and Y gets `+4`
- `-sourceScale` controls the source transform scale and defaults to `1.0`
- `-scale` controls the main transform scale and defaults to `1.0`
- `-trans` sets a secondary translation vector
- `-speed` writes parameter selector `0` with one float
- `-target` writes parameter selector `5` with one `vec3`
- `-hard` records a hard-start transition; absence records soft/default start
- the created effect reference is pushed into parser vector `+0x4B0`
- the matching transition type is pushed into parser vector `+0x4BC`
- `StartTestEffects()` later iterates those vectors and starts each stored test
  effect

Interpretation:

- `testEffect` is an author-side bootstrap / debugging hook
- it is likely intended to auto-spawn one or more effects when an `.fx` file is
  parsed or reloaded

### `messageTrigger`

Handler:

- `cMessageTriggerCommand::Parse(...)` at `0x007857BC`

Recovered syntax shape:

```text
messageTrigger <messageID> <effectName>
```

Recovered behavior:

- requires two non-switch arguments
- parses the first as a `uint` message ID
- parses the second as the target effect name
- constructs a `cSC4MessageTriggerDescription`
- adds that description to the active `cSC4EffectsCollection`

This is the author-side binding that later feeds the manager's trigger map and
`DoMessage()` auto-spawn path.

## Smallest Plausible Custom Wrapper

The simplest custom `main.fx` content we can justify from the recovered parser
and the Timbuktu syntax presentation is a wrapper around an already-existing
effect:

```text
effect my_wrapper
    visualEffect atmospheric_effects
end
```

This is still partly inferential because the binary recovery gives command and
argument semantics, while the `... end` block form comes from the Timbuktu
presentation rather than a decompiled block lexer. But it is now a defensible
first candidate:

- `effect my_wrapper` defines a new top-level visual effect
- `visualEffect atmospheric_effects` nests a reference to an existing effect
- `Effect my_wrapper` should then be the shortest manual in-game test

If `atmospheric_effects` turns out not to be a valid visual-effect description
name in the loaded collection, the same wrapper pattern should still work once
we substitute a known stock visual-effect name.
