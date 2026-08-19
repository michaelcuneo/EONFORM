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

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

The recipe/evaluator is therefore runtime-safe and editor-independent. Orakai will apply a constrained generation policy before evaluation rather than exposing unrestricted professional terrain authoring. See `Docs/ARCHITECTURE.md` and `Docs/ORAKAI_GENERATION_POLICY.md`.

## Current checkpoint: runtime-safe recipe and graph evaluator

This checkpoint begins replacing the monolithic host-project generation architecture with a reusable runtime graph engine in `CodenameGaeaCore`.

### Core hydraulic erosion ownership

Hydraulic erosion now has a runtime-safe Core implementation:

- `FGaeaHydraulicErosionSettings`
- `FGaeaHydraulicErosionResult`
- `FGaeaHydraulicErosion::ApplyInPlace()`
- `FGaeaHydraulicErosion::Evaluate()`
- `FGaeaHydraulicErosion::EvaluateWithArrays()`

The legacy project-level `FTerrainErosion` hydraulic API now delegates to this Core implementation. There is one authoritative hydraulic solver. Thermal erosion remains in the legacy project module for now.

### Runtime recipe data

`FGaeaTerrainRecipe` is plain runtime-safe data and contains:

- recipe version
- output node id
- stable `FGuid` node ids
- stable `FName` node type ids
- numeric/integer/bool/name parameter maps
- directed connections with named input/output ports

The recipe does not depend on `UObject`, `UEdGraph`, Slate, GraphEditor, UnrealEd, or Mesh Terrain.

Validation currently rejects:

- invalid/duplicate node ids
- missing output nodes
- invalid connections
- connections referencing missing nodes
- multiple edges targeting the same named input

Cycle detection occurs during evaluation.

### Deterministic recipe identity

`FGaeaTerrainRecipe::GetDeterministicHash()` hashes:

- recipe version
- output node
- node ids and node types
- typed parameters in lexical key order
- connections in stable order

Node and parameter storage ordering therefore does not change recipe identity. This is the first cache-identity primitive; future cache keys will additionally incorporate node implementation versions, input hashes, and evaluation domain/resolution.

### Runtime node registry

`FGaeaTerrainNodeRegistry` maps stable node type names to pure runtime evaluation functions.

Built-in node types in this first proof slice:

```text
SourceDataset
HydraulicErosion
```

The registry allows new node implementations to be added without changing recipe storage or making the recipe depend on editor classes.

### Runtime evaluator

`FGaeaTerrainEvaluator`:

- validates the recipe
- recursively resolves dependencies
- detects cycles
- memoizes each evaluated node for the duration of a graph run
- resolves node implementation through the runtime registry
- returns a final `FGaeaTerrainDataset`
- preserves the input source dataset as immutable graph input
- reports recipe hash and evaluation errors

The first executable graph is:

```text
SourceDataset
      |
      v
HydraulicErosion
      |
      v
FGaeaTerrainDataset
  Height
  Wear
  Deposits
  Flow
  + preserved input fields
```

The HydraulicErosion node consumes the dataset Height field and automatically uses compatible Rainfall, HydraulicErosion, Deposition, Evaporation, RockHardness, and SoilDepth fields when present.

## Automated coverage

New Core graph tests cover:

- valid recipe validation
- deterministic hash independence from node storage order
- rejection of duplicate input connections
- runtime SourceDataset -> HydraulicErosion evaluation
- Height/Wear/Deposits/Flow output presence
- source-dataset immutability
- graph cycle detection and error reporting

Existing spatial, dataset, semantic-field, and legacy hydraulic compatibility tests remain in place.

## Validation required before next step

This checkpoint adds multiple public Core types and new Core `.cpp` implementations, and changes the legacy hydraulic implementation to call into Core. Use a cold build rather than Live Coding.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `CodenameGaeaEditor` / Development Editor / Win64.
4. Open Unreal and confirm the existing terrain actor still generates normally.
5. Confirm `Tools -> Codename Gaea` still displays final fields including Wear/Deposits/Flow.
6. Run automation tests matching `CodenameGaea.Core.Graph` if convenient.
7. Run `CodenameGaea.Legacy.Erosion.HydraulicOutputs` if convenient to verify the compatibility wrapper remains identical.

## Next implementation step

After this graph-runtime checkpoint compiles, the next visible milestone is the first real professional graph authoring surface.

The editor will create a visual graph view over `FGaeaTerrainRecipe`, beginning with the two proven runtime node types rather than inventing a separate editor execution model. The first goal is to visually author and execute:

```text
Source Dataset -> Hydraulic Erosion
```

and publish its evaluated dataset into the existing Codename Gaea inspector.

After that, expand the runtime node library and migrate the remaining legacy terrain stages out of `ATerrainGeneratorActor` incrementally.
