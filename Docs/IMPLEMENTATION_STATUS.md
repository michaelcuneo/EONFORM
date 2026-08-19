# Codename Gaea Implementation Status

## Branches

- Protected baseline: `agent/terrain-foundation` at `439a8c14ab7b346ca8758223da2a3a2db66a650b`
- Active architecture branch: `agent/mesh-terrain-foundation`

The protected baseline must remain unchanged.

## Verified checkpoints

Compiled successfully in Unreal Engine 5.8 by the project owner:

- commercial plugin/module boundary
- `FGaeaGridDomain` / `FGaeaScalarField`
- shared-backed legacy `FTerrainHeightField`
- semantic context/process/geology fields
- `FGaeaTerrainDataset` and canonical field names
- runtime `FGaeaTerrainDatasetRegistry`
- first visible `Tools -> Codename Gaea` dockable dataset inspector
- first-class hydraulic erosion Height/Wear/Deposits/Flow outputs
- runtime-safe recipe/evaluator foundation

The following runtime graph automation tests have been run successfully by the project owner:

- `CodenameGaea.Core.Graph.RecipeValidation`
- `CodenameGaea.Core.Graph.SourceToHydraulicErosion`
- `CodenameGaea.Core.Graph.CycleDetection`

This establishes the runtime recipe/evaluator as a verified boundary before editor graph work begins.

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

The recipe/evaluator therefore remains runtime-safe and editor-independent. Orakai will apply a constrained generation policy before evaluation rather than exposing unrestricted professional terrain authoring. See `Docs/ARCHITECTURE.md` and `Docs/ORAKAI_GENERATION_POLICY.md`.

## Verified runtime graph foundation

### Core hydraulic erosion ownership

Hydraulic erosion has a runtime-safe Core implementation:

- `FGaeaHydraulicErosionSettings`
- `FGaeaHydraulicErosionResult`
- `FGaeaHydraulicErosion::ApplyInPlace()`
- `FGaeaHydraulicErosion::Evaluate()`
- `FGaeaHydraulicErosion::EvaluateWithArrays()`

The legacy project-level `FTerrainErosion` hydraulic API delegates to this Core implementation. There is one authoritative hydraulic solver. Thermal erosion remains in the legacy project module for now.

### Runtime recipe data

`FGaeaTerrainRecipe` is plain runtime-safe data containing recipe version, output node id, stable node ids/types, typed parameter maps, and named directed connections.

The recipe does not depend on `UObject`, `UEdGraph`, Slate, GraphEditor, UnrealEd, or Mesh Terrain.

### Runtime evaluator

`FGaeaTerrainEvaluator` validates recipes, resolves dependencies, detects cycles, memoizes node results for one evaluation, invokes runtime-registered node implementations, and returns an immutable-style `FGaeaTerrainDataset` handoff.

The first proven runtime graph is:

```text
SourceDataset -> HydraulicErosion
```

with output fields including Height, Wear, Deposits, and Flow.

## Current checkpoint: first professional graph UI

This checkpoint adds the first editor visualization of the verified runtime graph model. The runtime recipe/evaluator remains authoritative; the editor graph is a view and command surface over it.

### GraphEditor integration

`CodenameGaeaEditor` now depends on Unreal's `GraphEditor` module and adds editor-only graph types:

- `UGaeaEditorGraph`
- `UGaeaEditorGraphNode`
- `UGaeaEditorGraphSchema`

The current editor graph displays the two proven runtime node types:

```text
[Source Dataset] -> [Hydraulic Erosion]
```

The visual nodes use normal Unreal `UEdGraphNode` pins and an editor-only schema. They do not execute terrain logic themselves.

### Deliberately constrained first graph surface

The first graph topology is read-only while the recipe/editor synchronization contract is being validated.

Currently supported:

- visible native Unreal graph canvas
- Source Dataset node
- Hydraulic Erosion node
- real terrain connection pin/wire
- pan/zoom/select graph interaction
- `Evaluate Graph` action

Not enabled yet:

- arbitrary node creation
- node deletion/duplication
- rewiring that persists back into a recipe
- parameter editing
- saveable graph assets

Those capabilities come after this first visual/evaluation checkpoint compiles and is exercised successfully. This prevents the editor from representing states the runtime recipe cannot yet serialize faithfully.

### Runtime-backed evaluation

`Evaluate Graph` constructs/evaluates the same runtime `FGaeaTerrainRecipe` exercised by the Core automation tests.

The current source is the most recent `LegacyTerrainGenerator` snapshot. The result is published under:

```text
CodenameGaeaGraph
```

The existing field inspector refreshes automatically after graph evaluation, so Height/Wear/Deposits/Flow and other preserved fields can be inspected immediately.

### Dataset physical metadata

`FGaeaTerrainDatasetSnapshot` now carries runtime-safe metadata beginning with `HeightScale`.

The host terrain actor publishes its actual configured height scale with `LegacyTerrainGenerator`, and graph evaluation carries that metadata into `CodenameGaeaGraph`. This prevents hydraulic slope/capacity calculations from silently using a hardcoded physical height scale.

## Validation required before next step

This checkpoint adds UHT-generated editor UObject types, new GraphEditor source files, a new module dependency, and changes the runtime snapshot ABI. Use a cold build.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `CodenameGaeaEditor` / Development Editor / Win64.
4. Open Unreal and regenerate the current terrain actor.
5. Open `Tools -> Codename Gaea`.
6. Confirm the upper graph area displays `Source Dataset -> Hydraulic Erosion`.
7. Confirm the source header reports the actor's actual height scale (default foundation value is 8000 cm unless changed).
8. Click `Evaluate Graph`.
9. Confirm status reports a successful recipe hash/revision and the inspector source changes to `CodenameGaeaGraph` automatically.
10. Inspect Height, Wear, Deposits, and Flow after graph evaluation.
11. Confirm the existing terrain actor itself remains visually unchanged; this graph evaluation currently publishes data for inspection and does not replace the actor mesh.

## Next implementation step

After this visual graph checkpoint is verified, make the editor graph genuinely authorable while preserving one-way truth through `FGaeaTerrainRecipe`:

1. add runtime node descriptors/port descriptors
2. generate editor nodes from descriptors rather than hard-coded editor cases
3. add node creation menu
4. synchronize graph connections back into runtime recipe data
5. add selected-node parameter inspector and validation
6. introduce a saveable recipe/graph asset

Only after those are stable should the larger legacy generation stages be migrated into graph nodes and the actor cease being the primary authoring architecture.
