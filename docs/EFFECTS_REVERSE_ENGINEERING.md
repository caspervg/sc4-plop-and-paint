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
