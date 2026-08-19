# Codename Gaea Implementation Status

## Branches

- Protected baseline: `agent/terrain-foundation` at `439a8c14ab7b346ca8758223da2a3a2db66a650b`
- Active architecture branch: `agent/mesh-terrain-foundation`

The protected baseline must remain unchanged.

## Verified checkpoint: plugin boundary

The following structural milestone has been compiled successfully in Unreal Engine 5.8 by the project owner:

- `Plugins/CodenameGaea/CodenameGaea.uplugin`
- `CodenameGaeaCore`
- `CodenameGaeaRuntime`
- `CodenameGaeaEditor`
- host-project plugin enablement

## Current checkpoint: spatial primitives

The active branch now introduces the first shared terrain-data primitives in `CodenameGaeaCore`.

### `FGaeaGridDomain`

Defines a regular 2D world-space sample domain with:

- rectangular interior dimensions
- explicit `WorldMin` / `WorldMax`
- derived cell spacing
- optional guard-band samples
- interior and storage coordinate/index helpers
- sample-to-world and world-to-sample transforms
- evaluation bounds that include guard storage

`Dimensions` always describes the requested interior grid. `BorderSamples` expands evaluation/storage outside that requested region without changing interior resolution or spacing.

A default-constructed domain is invalid by design.

### `FGaeaScalarField`

Defines dense scalar values over an `FGaeaGridDomain` with:

- field name
- semantic unit
- nearest or bilinear interpolation
- guard-band-aware storage
- interior/storage accessors
- world-space sampling
- explicit clamp/no-clamp behavior outside the evaluation domain

Current unit metadata includes unitless, normalized, centimetres, metres, degrees, and Celsius.

### Legacy compatibility seam

`FTerrainHeightField` remains structurally unchanged so existing terrain generation, erosion, geology, and hydrology code continue to use the exact same storage and access patterns.

It now exposes:

- `GetGaeaDomain()`
- `ToGaeaScalarField()`

The conversion maps the existing centered square terrain domain from `-WorldSize/2` to `+WorldSize/2` and marks legacy height values as normalized.

This is intentionally one-way for now. Replacing `FTerrainHeightField` storage with `FGaeaScalarField` is deferred until the new primitives compile and their behavior is validated.

## Automated coverage

Development automation tests currently cover:

- domain validity
- guard-band storage dimensions
- cell spacing
- evaluation bounds
- interior/storage world mapping
- scalar-field validity
- bilinear center sampling
- explicit out-of-domain clamp behavior

## Validation required before next step

1. Pull `agent/mesh-terrain-foundation`.
2. Regenerate project files if Unreal Build Tool requests it.
3. Build `CodenameGaeaEditor` / Development Editor / Win64 in UE 5.8.
4. Run automation tests matching `CodenameGaea.Core` if convenient.
5. Do not proceed with migration if the module or tests fail.

## Next implementation step

Once this checkpoint compiles, migrate `FTerrainHeightField` incrementally onto the shared field/domain model while preserving terrain output and all existing algorithm behavior.

After that migration is verified, move context/geology outputs into first-class fields before changing erosion semantics or introducing graph execution.
