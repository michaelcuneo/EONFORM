# Codename Gaea Architecture

## Purpose

Codename Gaea is being built for two simultaneous goals:

1. Serve as the terrain/world-generation foundation for Orakai.
2. Ship independently as a reusable Unreal Engine plugin.

The commercial plugin must remain generic. Orakai may depend on Codename Gaea, but Codename Gaea must never depend on Orakai.

A critical Orakai requirement is that terrain generation is not only an offline authoring step. Orakai players will create their own islands in the packaged game and then play on those generated islands. Therefore the terrain recipe, graph evaluator, simulation core, caching model, and at least one geometry materialization backend must be runtime-capable.

## Protected baseline

The known-good terrain foundation is commit `439a8c14ab7b346ca8758223da2a3a2db66a650b` on `agent/terrain-foundation`.

New architecture work proceeds on `agent/mesh-terrain-foundation`. The baseline branch is not to be modified.

## Architectural principles

- The terrain compute core is independent from UE 5.8 Mesh Terrain APIs.
- Mesh Terrain is the preferred UE 5.8 editor authoring/output backend, not the authoritative simulation model and not the only geometry backend.
- Player-created islands must be generatable in a packaged runtime build.
- The graph evaluation model and terrain recipes must not depend on UnrealEd, Slate, GraphEditor, or other editor-only APIs.
- Editor-only and experimental engine APIs are isolated from runtime/core code.
- Terrain node outputs are first-class data, not transient arrays hidden inside algorithms.
- Spatial fields carry domain, resolution, bounds, units, and interpolation semantics.
- Node evaluation is deterministic where practical and cacheable by dependencies, parameters, domain, and version.
- Completed node results are treated as immutable values.
- Runtime gameplay queries use Codename Gaea's own generated/baked runtime data, not Mesh Terrain channels as the source of truth.
- Orakai-specific systems consume public Codename Gaea APIs through a separate Orakai integration layer.
- A saved island should be representable primarily by its deterministic recipe/seed/parameters plus any explicit user edits, rather than requiring the entire generated terrain to be the canonical save format.

## Plugin module layout

The repository remains a development host project, while reusable product code lives under `Plugins/CodenameGaea`.

```text
Plugins/CodenameGaea/
  CodenameGaea.uplugin
  Source/
    CodenameGaeaCore/
    CodenameGaeaRuntime/
    CodenameGaeaEditor/
```

### CodenameGaeaCore

Pure terrain technology and runtime-safe graph-domain primitives:

- spatial domains and grid definitions
- scalar/vector/category fields
- terrain datasets
- graph evaluation primitives
- deterministic terrain recipes and evaluation inputs
- noise and shaping
- structure and geology
- context analysis
- erosion and sediment transport
- hydrology
- climate and biome calculations
- cache/hash primitives
- sampling and interpolation

This module must not depend on Mesh Terrain, Slate, UnrealEd, GraphEditor, or editor-only APIs.

### CodenameGaeaRuntime

Unreal runtime-facing integration:

- world/subsystem query facade
- runtime island generation orchestration
- runtime terrain recipe instances
- generated/baked terrain data assets where useful
- loaded terrain-region management
- runtime geometry materialization backends
- Blueprint/query helpers where appropriate
- runtime sampling for Orakai and third-party games

The first stock-engine runtime geometry backend will build on Unreal's runtime `UDynamicMesh` / `UDynamicMeshComponent` APIs. It is a correctness and vertical-slice backend, not a permanent assumption that one monolithic Dynamic Mesh is the final large-island rendering architecture. Chunking, streaming, collision, LOD, and higher-performance runtime representation remain separate scalability work.

### CodenameGaeaEditor

Authoring-only functionality:

- terrain graph editor UI
- node inspectors and previews
- build/bake/cache controls
- profiling and diagnostics
- Mesh Terrain adapter/modifiers
- Mesh Partition weight-channel publishing
- PCG/editor integration

The editor graph UI edits runtime-safe recipe/graph data. The editor UI itself is not required for evaluating that recipe.

## Terrain data model direction

The original foundation used `FTerrainHeightField` and many subsystem-specific `TArray<float>` collections. These are being migrated incrementally toward a shared typed spatial data model.

Planned core types:

```text
FGaeaGridDomain
FGaeaScalarField
FGaeaVectorField
FGaeaCategoryField
FGaeaTerrainDataset
```

A domain must eventually describe:

- dimensions
- world bounds
- cell size
- transform/projection information where needed
- evaluation guard/border samples

Fields must carry semantic metadata such as name, units, interpolation behavior, and domain.

This permits different systems to evaluate at different resolutions, for example climate at lower resolution than erosion, while retaining correct resampling and spatial meaning.

## Graph execution model

Nodes consume immutable inputs and produce immutable outputs. A node may expose multiple independently addressable outputs.

Example erosion outputs:

```text
Height
Wear
Deposits
Flow
```

Cache identity should eventually include at least:

```text
Node type
Node implementation/version
Parameters
Input hashes
Evaluation domain
Resolution
```

Dirty propagation follows graph dependencies. Preview-resolution evaluation and high-resolution/runtime island evaluation are separate modes of the same graph evaluator.

The evaluator belongs in runtime-safe code. `UEdGraph`/GraphEditor objects may provide an editor representation, but they must not become the execution model.

## Player-created island model

Orakai's pull is player-authored islands. The intended data flow is:

```text
Player choices / seed / presets / edits
                 |
                 v
        Runtime-safe Gaea recipe
                 |
                 v
          Graph evaluation
                 |
                 v
        FGaeaTerrainDataset
          /             \
         v               v
Runtime geometry      Gameplay fields
backend               climate/biome/etc.
         \               /
          v             v
            Playable island
```

The Orakai UI is free to expose a curated island creator rather than the full professional node editor. Both should drive the same underlying runtime-safe recipe and compute engine.

This means a commercial plugin customer can use the professional editor graph, while Orakai can expose approachable controls such as island size, mountain character, erosion age, climate, biome balance, rivers, seed, and other higher-level choices without forking the terrain engine.

## Geometry backend strategy

### Mesh Terrain editor backend

UE 5.8 Mesh Terrain remains the preferred high-end authored terrain representation for editor workflows. The editor integration will eventually:

- deform Mesh Terrain geometry from Codename Gaea datasets
- publish selected terrain fields into Mesh Partition weight channels
- preserve editor-only intermediate channels where runtime compilation is unnecessary
- keep the compute core independent from MeshPartition APIs

Mesh Terrain's modifier authoring API is editor-only in UE 5.8, so it cannot be the sole materialization path for Orakai's packaged player-created islands.

### Runtime Dynamic Mesh backend

Unreal's `UDynamicMesh` and `UDynamicMeshComponent` are runtime GeometryFramework types. Codename Gaea will use them as the first runtime materialization backend so a generated dataset can become playable geometry in a packaged game.

This backend must eventually address:

- chunked generation rather than one giant mesh
- asynchronous build staging
- collision generation/update policy
- material/field transfer
- streaming and unload/rebuild
- LOD/performance strategy
- persistence via recipe rather than only serialized mesh data

Dynamic Mesh is therefore both a runtime vertical-slice backend and a useful preview/debug backend. Heightmap/mask export and optional legacy Landscape output remain possible adapters.

## Runtime/Orakai strategy

Orakai should query Codename Gaea through generic runtime APIs rather than Mesh Terrain internals.

Conceptual usage:

```cpp
FGaeaTerrainSample Sample = TerrainSubsystem->Sample(WorldPosition);
```

A sample may eventually expose data such as height, slope, flow, wetness, geology, soil, climate, and biome values.

This enables terrain data to drive vegetation, traversal, AI, resources, rivers, snow, ambient systems, and other gameplay without embedding Orakai-specific logic in the commercial plugin.

For generated islands, the same terrain dataset used to create geometry should remain available to downstream gameplay systems. Visual terrain and gameplay terrain knowledge must derive from the same deterministic island recipe.

## Implementation sequence

Each step is intentionally small and should compile/validate before the next one begins.

1. Establish `Plugins/CodenameGaea` and Core/Runtime/Editor module boundaries. **Done.**
2. Introduce grid/domain and scalar-field primitives. **Done.**
3. Migrate `FTerrainHeightField` onto the new field/domain model without changing behavior. **Done.**
4. Migrate context, process-mask, and geology outputs into first-class fields. **Current.**
5. Introduce a named typed terrain dataset.
6. Build the first visible Codename Gaea editor window and field inspector using the real dataset.
7. Change erosion to expose first-class multi-output results, especially Height/Wear/Deposits/Flow.
8. Introduce the runtime-safe terrain recipe/graph evaluator and deterministic cache identity.
9. Build the minimal professional node graph UI as an editor view over the runtime evaluator.
10. Add a runtime island-generation orchestration path and first Dynamic Mesh materialization backend.
11. Add the UE 5.8 Mesh Terrain editor adapter/modifier backend.
12. Add dirty propagation, async evaluation, cancellation, preview/runtime resolution modes, and tiled/guard-band execution.
13. Add the generic runtime terrain-query subsystem and generated-region management.
14. Expand node library, runtime island-authoring controls/API, export backends, and production UX.

## Current milestone

The module boundary, spatial primitives, and shared-backed legacy heightfield are verified compiling in UE 5.8.

The current implementation milestone is to convert context, process-mask, and geology output maps from anonymous arrays into `FGaeaScalarField` instances with stable names, units, domain metadata, and sampling behavior, while keeping the existing terrain pipeline behavior unchanged.
