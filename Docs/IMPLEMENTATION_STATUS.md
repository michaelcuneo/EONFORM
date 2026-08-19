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

The editor surface is now visibly available and reads real generated terrain datasets through the runtime registry without depending on the host project's terrain actor class.

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

The graph/recipe evaluator therefore remains runtime-safe. Orakai will apply a constrained generation policy before evaluation rather than exposing unrestricted professional terrain authoring. See `Docs/ARCHITECTURE.md` and `Docs/ORAKAI_GENERATION_POLICY.md`.

## Current checkpoint: hydraulic erosion multi-output

Hydraulic erosion now exposes first-class outputs matching the architecture direction:

```text
Height
Wear
Deposits
Flow
```

### Output semantics

- `Height` — post-hydraulic normalized terrain height
- `Wear` — cumulative normalized material removed from each sample during the hydraulic simulation
- `Deposits` — cumulative normalized material deposited at each sample during the hydraulic simulation
- `Flow` — cumulative moved-water accumulation used by the existing hydrology pipeline

`Wear`, `Deposits`, and `Flow` are observations of values already produced by the existing solver. The erosion/deposition formulas themselves are unchanged.

### API shape

`FTerrainErosion::ApplyHydraulic(...)` remains the legacy mutating compatibility API. It now optionally exposes wear and deposit arrays in addition to the existing flow accumulation output.

`FTerrainErosion::EvaluateHydraulic(...)` is the graph-facing direction:

- accepts a const input heightfield
- copies the input internally
- runs the same hydraulic solver used by the legacy path
- returns `FTerrainHydraulicErosionResult`
- does not mutate its input

`FTerrainHydraulicErosionResult` owns first-class `FGaeaScalarField` values for Height/Wear/Deposits/Flow.

### Dataset publication

The temporary legacy dataset publication has been moved out of `FTerrainContext::BuildProcessMasks()`.

`ATerrainGeneratorActor::BuildTerrain()` now owns the end-of-generation publication point. It publishes one coherent dataset after hydraulic erosion, river carving, and mesh generation.

The published dataset now includes:

- final Height
- context fields
- process-mask fields
- geology fields
- Wear/Deposits/Flow when hydraulic erosion is enabled and produces a valid result

This means the existing `Tools -> Codename Gaea` inspector should show the erosion outputs immediately after regeneration and Refresh.

## Automated coverage

The new `CodenameGaea.Legacy.Erosion.HydraulicOutputs` regression test verifies:

- `EvaluateHydraulic()` succeeds on valid input
- the returned multi-output result is valid
- evaluation does not mutate the input heightfield
- stable output names Height/Wear/Deposits/Flow
- legacy `ApplyHydraulic()` and `EvaluateHydraulic()` produce matching Height, Flow, Wear, and Deposits values for identical inputs/settings

Existing core/dataset/legacy field tests remain in place.

## Validation required before next step

This checkpoint changes public headers and cross-module canonical field symbols, so use a cold build.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `CodenameGaeaEditor` / Development Editor / Win64.
4. Open Unreal and regenerate the terrain actor.
5. Open `Tools -> Codename Gaea` and press Refresh.
6. With hydraulic erosion enabled, confirm `Wear`, `Deposits`, and `Flow` appear and preview correctly.
7. Confirm geology fields are now present in the final snapshot as well.
8. Confirm visible terrain remains unchanged.
9. Run `CodenameGaea.Legacy.Erosion.HydraulicOutputs` if convenient.

## Next implementation step

After this checkpoint is verified, begin the runtime-safe recipe/graph evaluation foundation.

The first graph milestone should be deliberately small: runtime node identifiers/descriptors, typed input/output ports, a deterministic recipe representation, and a minimal evaluator capable of expressing a short terrain chain without relying on `UEdGraph` or editor-only APIs.

The professional editor graph will later be a visual authoring view over that same runtime recipe/evaluator.
