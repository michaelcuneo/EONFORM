# Codename Gaea Implementation Status

## Branches

- Protected baseline: `agent/terrain-foundation` at `439a8c14ab7b346ca8758223da2a3a2db66a650b`
- Active architecture branch: `agent/mesh-terrain-foundation`

The protected baseline must remain unchanged.

## Verified checkpoints

### Plugin boundary

Compiled successfully in Unreal Engine 5.8 by the project owner:

- `Plugins/CodenameGaea/CodenameGaea.uplugin`
- `CodenameGaeaCore`
- `CodenameGaeaRuntime`
- `CodenameGaeaEditor`
- host-project plugin enablement

### Spatial primitives

Compiled successfully in Unreal Engine 5.8 by the project owner:

- `FGaeaGridDomain`
- `FGaeaScalarField`
- field metadata for units/interpolation
- guard-band-aware storage and sampling

### Heightfield storage migration

Compiled successfully in Unreal Engine 5.8 by the project owner:

- `FTerrainHeightField` backed internally by `FGaeaScalarField`
- legacy `Resolution`, `WorldSize`, `Data`, `Index()`, and `At()` compatibility preserved
- copy/move value semantics verified

### Semantic terrain fields

Compiled successfully in Unreal Engine 5.8 by the project owner:

- context outputs backed by named `FGaeaScalarField` values
- process-mask outputs backed by named `FGaeaScalarField` values
- geology outputs backed by named `FGaeaScalarField` values
- legacy array aliases preserved
- no terrain formulas changed

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

This is now a hard architecture requirement:

- graph/recipe evaluation must be runtime-safe
- terrain recipes and deterministic evaluation cannot depend on editor-only graph objects
- Mesh Terrain remains an editor authoring/output backend, not the only materialization path
- the first runtime materialization backend will use Unreal runtime Dynamic Mesh APIs
- generated gameplay fields and generated geometry derive from the same terrain dataset/recipe
- saved islands should prefer deterministic recipe/seed/parameters plus explicit edits over treating serialized final geometry as the only canonical representation

Orakai will not expose unrestricted professional generation. A runtime generation policy/profile will constrain island extent, resolution, permitted operations, parameter ranges, simulation effort, and overall complexity before evaluation. See `Docs/ORAKAI_GENERATION_POLICY.md`.

## Current checkpoint: terrain dataset

`CodenameGaeaCore` now contains `FGaeaTerrainDataset`, a generic named collection of terrain fields.

Current behavior:

- owns scalar fields by value
- accepts fields with different valid domains/resolutions
- uses each field descriptor name as the stable lookup key
- exposes const lookup for downstream consumers
- supports field replacement/removal/reset
- supports deterministic lexical field-name enumeration
- supports world-space sampling by field name
- rejects invalid or unnamed fields

Canonical built-in field names now live in `GaeaTerrainFieldNames` rather than requiring consumers to repeat string literals.

The current canonical set covers:

- Height
- Elevation
- SlopeDegrees
- Concavity
- Convexity
- Mountain
- Foothill
- Plains
- Thermal
- Rainfall
- HydraulicErosion
- Deposition
- Evaporation
- RockHardness
- Weathering
- SoilDepth

A temporary host-project `FTerrainDatasetBridge` packages the existing legacy generation outputs into an independent `FGaeaTerrainDataset`. This bridge exists only during migration; the final graph/evaluator will produce datasets directly inside the plugin.

## Automated coverage

Core spatial tests cover domain geometry and scalar-field sampling.

Dataset tests cover:

- named insertion and lookup
- multiple fields with different resolutions/domains
- world-space sampling
- lexical name enumeration
- same-name replacement without field-count growth
- removal/reset
- missing-field failure behavior

Legacy/regression tests continue to cover heightfield compatibility and semantic context/geology/process fields.

## Validation required before next step

1. Pull `agent/mesh-terrain-foundation`.
2. Build `CodenameGaeaEditor` / Development Editor / Win64 in UE 5.8.
3. Run automation tests matching `CodenameGaea.Core` and `CodenameGaea.Legacy` if convenient.
4. Confirm existing terrain generation remains visually unchanged.
5. Do not proceed if compilation, tests, or terrain behavior regress.

## Next implementation step

Once this checkpoint is verified, build the first visible Codename Gaea editor surface in `CodenameGaeaEditor`:

- `Tools -> Codename Gaea`
- dockable editor tab
- real terrain-dataset field list
- selected-field metadata/domain inspector
- initial scalar-field visualization path

The first editor milestone should consume the real `FGaeaTerrainDataset` model rather than mock data. After that visible checkpoint, erosion will be upgraded to expose first-class Height/Wear/Deposits/Flow outputs and the runtime-safe recipe/graph evaluator will begin replacing the legacy monolithic actor pipeline.
