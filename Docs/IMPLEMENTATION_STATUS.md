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

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

This is now a hard architecture requirement:

- graph/recipe evaluation must be runtime-safe
- terrain recipes and deterministic evaluation cannot depend on editor-only graph objects
- Mesh Terrain remains an editor authoring/output backend, not the only materialization path
- the first runtime materialization backend will use Unreal runtime Dynamic Mesh APIs
- generated gameplay fields and generated geometry derive from the same terrain dataset/recipe
- saved islands should prefer deterministic recipe/seed/parameters plus explicit edits over treating serialized final geometry as the only canonical representation

See `Docs/ARCHITECTURE.md` for the full runtime/editor/backend split.

## Current checkpoint: semantic terrain fields

Context, process masks, and geology now produce first-class `FGaeaScalarField` outputs while retaining their original `TArray<float>` names as compatibility aliases.

### Context fields

- `Elevation` — normalized
- `SlopeDegrees` — degrees
- `Concavity` — normalized
- `Convexity` — normalized
- `Mountain` — normalized
- `Foothill` — normalized
- `Plains` — normalized

Each is backed by a correspondingly named `FGaeaScalarField` and shares the exact same value buffer with the legacy array alias.

### Process-mask fields

- `Thermal`
- `Rainfall`
- `HydraulicErosion`
- `Deposition`
- `Evaporation`

All are normalized scalar fields over the same domain as the heightfield.

### Geology fields

- `RockHardness`
- `Weathering`
- `SoilDepth`

All are normalized scalar fields over the same domain as the heightfield.

`FGaeaGridDomain` now has explicit equality operators so semantic field validation can verify exact domain identity rather than only matching buffer lengths.

No context, process-mask, or geology formulas were changed in this migration.

## Automated coverage

Core spatial tests cover:

- domain validity
- domain equality
- guard-band storage dimensions
- cell spacing
- evaluation bounds
- interior/storage world mapping
- scalar-field validity
- bilinear center sampling
- explicit out-of-domain clamp behavior

Legacy/regression tests cover:

- legacy heightfield storage aliasing the new scalar field
- heightfield copy/move value semantics
- context fields validating against the heightfield domain
- stable context field names/units
- geology fields validating against the heightfield domain
- stable geology field names/units
- process-mask fields validating against the heightfield domain
- compatibility arrays sharing the exact same field buffers

## Validation required before next step

1. Pull `agent/mesh-terrain-foundation`.
2. Build `CodenameGaeaEditor` / Development Editor / Win64 in UE 5.8.
3. Run automation tests matching `CodenameGaea.Core` and `CodenameGaea.Legacy` if convenient.
4. Confirm existing terrain generation remains visually unchanged.
5. Do not proceed if compilation, tests, or terrain behavior regress.

## Next implementation step

After this checkpoint is verified, introduce `FGaeaTerrainDataset`: a named typed collection that owns and exposes terrain fields independently of the legacy subsystem structs.

That dataset is the key seam for both upcoming visible editor inspection and Orakai runtime island generation. Once the dataset compiles, the next milestone is the first real Codename Gaea editor window/field inspector so the plugin becomes visibly inspectable inside UE rather than remaining infrastructure-only.
