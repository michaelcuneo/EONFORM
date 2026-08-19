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

### Terrain dataset

Compiled successfully in Unreal Engine 5.8 by the project owner using a cold build after the module-boundary change:

- `FGaeaTerrainDataset`
- canonical `GaeaTerrainFieldNames`
- mixed-resolution field support
- deterministic field lookup/enumeration/sampling
- temporary legacy `FTerrainDatasetBridge`

The Live Coding linker failure observed at this checkpoint was caused by rebuilding the host patch against newly-added Core DLL symbols. A cold UE build succeeded, confirming the Core/Runtime module boundary is valid.

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

This is a hard architecture requirement:

- graph/recipe evaluation must be runtime-safe
- terrain recipes and deterministic evaluation cannot depend on editor-only graph objects
- Mesh Terrain remains an editor authoring/output backend, not the only materialization path
- the first runtime materialization backend will use Unreal runtime Dynamic Mesh APIs
- generated gameplay fields and generated geometry derive from the same terrain dataset/recipe
- saved islands should prefer deterministic recipe/seed/parameters plus explicit edits over treating serialized final geometry as the only canonical representation

Orakai will not expose unrestricted professional generation. A runtime generation policy/profile will constrain island extent, resolution, permitted operations, parameter ranges, simulation effort, and overall complexity before evaluation. See `Docs/ORAKAI_GENERATION_POLICY.md`.

## Current checkpoint: first visible editor surface

The first inspectable Codename Gaea editor workflow is now implemented.

### Runtime dataset handoff

`CodenameGaeaRuntime` now contains `FGaeaTerrainDatasetRegistry`.

It provides a runtime-safe, producer-agnostic handoff between terrain generation and downstream consumers:

- publish by source id
- monotonically increasing revision numbers
- latest-dataset lookup
- source-specific lookup/removal
- copied immutable snapshots for consumers
- no dependency on the host project's terrain actor

The current legacy pipeline temporarily publishes its latest Height/Context/Process dataset under `LegacyTerrainGenerator`. This publication is a migration seam only; the future graph evaluator will publish datasets directly from plugin code.

### Editor tab

`CodenameGaeaEditor` now registers:

```text
Tools -> Codename Gaea
```

The command opens a dockable Nomad tab containing the initial terrain dataset inspector.

The inspector currently provides:

- latest source id and revision
- scalar-field count
- deterministic field list
- selected field name
- semantic unit
- interpolation mode
- field resolution
- guard-band count
- world-space bounds
- cell size
- 32 x 32 grayscale scalar preview sampled from the real generated field
- Refresh button for newly generated/rebuilt terrain

The editor module consumes only `CodenameGaeaRuntime`/`CodenameGaeaCore`; it does not depend on `ATerrainGeneratorActor` or other host-project classes.

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

This checkpoint changes module dependencies and adds new Runtime/Editor source files, so use a cold build rather than Live Coding.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `CodenameGaeaEditor` / Development Editor / Win64 in UE 5.8.
4. Open the editor.
5. Generate/regenerate the current terrain actor so a dataset is published.
6. Open `Tools -> Codename Gaea`.
7. Press Refresh if the tab was open before generation.
8. Confirm the field list and grayscale previews update when selecting Height/Elevation/Slope/etc.
9. Confirm existing terrain generation remains visually unchanged.

## Next implementation step

Once the visible inspector checkpoint is verified, the next core change is erosion multi-output data:

```text
Height
Wear
Deposits
Flow
```

Those outputs will become first-class scalar fields instead of transient internal arrays. The visible inspector will then immediately let us inspect the new erosion products before the runtime-safe recipe/graph evaluator and professional node graph are introduced.
