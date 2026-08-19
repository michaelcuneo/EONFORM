# Codename Gaea Architecture

## Purpose

Codename Gaea is being built for two simultaneous goals:

1. Serve as the terrain/world-generation foundation for Orakai.
2. Ship independently as a reusable Unreal Engine plugin.

The commercial plugin must remain generic. Orakai may depend on Codename Gaea, but Codename Gaea must never depend on Orakai.

## Protected baseline

The known-good terrain foundation is commit `439a8c14ab7b346ca8758223da2a3a2db66a650b` on `agent/terrain-foundation`.

New architecture work proceeds on `agent/mesh-terrain-foundation`. The baseline branch is not to be modified.

## Architectural principles

- The terrain compute core is independent from UE 5.8 Mesh Terrain APIs.
- Mesh Terrain is the preferred UE 5.8 authoring/output backend, not the authoritative simulation model.
- Editor-only and experimental engine APIs are isolated from runtime/core code.
- Terrain node outputs are first-class data, not transient arrays hidden inside algorithms.
- Spatial fields carry domain, resolution, bounds, units, and interpolation semantics.
- Node evaluation is deterministic where practical and cacheable by dependencies, parameters, domain, and version.
- Completed node results are treated as immutable values.
- Runtime gameplay queries use Codename Gaea's own baked/runtime data, not Mesh Terrain channels as the source of truth.
- Orakai-specific systems consume public Codename Gaea APIs through a separate Orakai integration layer.

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

Pure terrain technology and graph-domain primitives:

- spatial domains and grid definitions
- scalar/vector/category fields
- terrain datasets
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
- baked terrain data assets
- loaded terrain-region management
- Blueprint/query helpers where appropriate
- runtime sampling for Orakai and third-party games

### CodenameGaeaEditor

Authoring-only functionality:

- terrain graph editor
- node inspectors and previews
- build/bake/cache controls
- profiling and diagnostics
- Mesh Terrain adapter/modifiers
- Mesh Partition weight-channel publishing
- PCG/editor integration

## Terrain data model direction

The current foundation uses `FTerrainHeightField` and many subsystem-specific `TArray<float>` collections. These will be migrated incrementally toward a shared typed spatial data model.

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

Dirty propagation follows graph dependencies. Preview-resolution evaluation and high-resolution/baked evaluation are separate modes of the same graph.

## Mesh Terrain strategy

UE 5.8 Mesh Terrain is the preferred authored terrain representation, but remains an adapter/backend.

The editor integration will eventually:

- deform Mesh Terrain geometry from Codename Gaea datasets
- publish selected terrain fields into Mesh Partition weight channels
- preserve editor-only intermediate channels where runtime compilation is unnecessary
- keep the compute core independent from MeshPartition APIs

Dynamic Mesh remains useful as a preview/debug backend. Heightmap/mask export and optional legacy Landscape output remain possible adapters.

## Runtime/Orakai strategy

Orakai should query Codename Gaea through generic runtime APIs rather than Mesh Terrain internals.

Conceptual usage:

```cpp
FGaeaTerrainSample Sample = TerrainSubsystem->Sample(WorldPosition);
```

A sample may eventually expose data such as height, slope, flow, wetness, geology, soil, climate, and biome values.

This enables terrain data to drive vegetation, traversal, AI, resources, rivers, snow, ambient systems, and other gameplay without embedding Orakai-specific logic in the commercial plugin.

## Implementation sequence

Each step is intentionally small and should compile/validate before the next one begins.

1. Establish `Plugins/CodenameGaea` and Core/Runtime/Editor module boundaries.
2. Introduce grid/domain and scalar-field primitives.
3. Migrate `FTerrainHeightField` onto the new field/domain model without changing behavior.
4. Migrate context and geology outputs into first-class fields.
5. Change erosion to expose first-class multi-output results, especially Height/Wear/Deposits/Flow.
6. Introduce a named typed terrain dataset.
7. Add the generic runtime terrain-query API.
8. Add the UE 5.8 Mesh Terrain adapter/modifier integration.
9. Introduce the terrain graph asset and evaluator.
10. Add cache identity, dirty propagation, async evaluation, cancellation, and preview/build resolution modes.
11. Build the graph editor UI and authoring workflows.

## Current milestone

The first implementation milestone is deliberately structural only:

- create the plugin descriptor
- create empty Core, Runtime, and Editor modules
- enable the plugin in the development host project
- do not move or rewrite terrain algorithms yet

Only after this boundary is verified should existing terrain code begin migrating into the plugin.
