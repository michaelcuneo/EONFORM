# Regional Evaluation and Optimisation Handoff

Last updated: 2026-08-30

Active branch: `agent/regional-evaluation-foundation`

Base before the current optimisation pass: `264a33e948d0883931a174ffb4007d24d061ba47` (`WIP preserve local work before regional branch sync`)

This document is the durable restart point for regional evaluation, world-scale optimisation, Terrain View preparation, and exact whole-world semantics. A future conversation should read this file before changing regional evaluation code.

## 1. Product goal

EONFORM must stop treating a generated world as one giant always-resident terrain raster/mesh.

The intended runtime model is:

```text
whole procedural world definition
        |
        +-> cheap global/reference state
        +-> low-resolution world / Terrain View representation
        +-> exact cached regional evaluations
        +-> high-detail regional evaluation near demand
        +-> Mesh Terrain / Mesh Partition materialisation
        +-> collision / PCG / gameplay only for active regions
```

A terrain region may be known or cached without being rendered, collidable, or materialised at full detail.

Useful conceptual states are:

```text
Known
Evaluated low-res
Cached
Materialized
Active
Visible
High-detail
```

Do not collapse these states into a single "terrain exists" flag.

## 2. Non-negotiable architecture rules

### 2.1 Node authority

If an operation exists conceptually as a graph node, the authoritative implementation belongs to that node.

Composite nodes may call the authoritative node implementation. They must not maintain private behavioural copies of the same operation.

Examples:

```text
Blur node            owns Blur radius/behaviour
Denoise node         owns Denoise radius/behaviour
DirectionalWarp node owns Directional Warp behaviour
AutoLevel node       owns AutoLevel remap behaviour
Ridge                composes authoritative operations
Mountain             composes Ridge and other authoritative operations
```

Infrastructure may schedule, cache, reduce, or route an operation, but must not redefine its terrain semantics.

### 2.2 Exactness before admission

A node is admitted to independent regional evaluation only after its regional output is proven equivalent to full-world evaluation for the same reference lattice.

Do not add an approximation merely to make `FEonformTerrainRegionalSupport::Analyze()` return supported.

Unsupported is preferable to silently different terrain.

### 2.3 Halo accounting is path-aware

Neighbourhood operations publish their required local border/halo. The regional support planner accumulates borders along graph dependencies.

Examples already regionalised include:

- Blur
- Denoise
- Sharpen
- Slope
- Curvature
- Angle

The scheduler supplies the accumulated external border. Operators process their actual storage including guard samples while respecting true full-world edges.

### 2.4 Global semantics do not require a full-world raster

Operations that require whole-world scalar summaries should resolve them through bounded streaming reductions and cache the result for the generation plan.

Current exact examples:

- AutoLevel upstream min/max
- Mountain core min/max
- Terrain Context global Height min/max when supplied through evaluation scope

A global requirement is not permission to allocate the entire world raster.

## 3. Completed regional optimisation passes

### Pass A - Regional Mountain through sparse Ridge sampling

Commit: `1ba5cc8` - `Enable regional Mountain evaluation through Ridge support`

Key behaviour:

- Ridge can sample only the required coordinates in the virtual full-world reference lattice.
- Mountain reconstructs its core from sparse Ridge samples for a target region.
- Displaced/bilinear dependencies are gathered before Ridge sampling.
- Shared summary cache prevents repeated whole-world scalar work across regions.

Important limitation retained deliberately:

- arbitrary non-aligned target grids are not the proven contract; regional generation assumes grids aligned to the reference lattice.

### Pass B - Exact regional Blur dependencies

Commit: `660dd1d` - `Enable exact regional Blur dependencies`

Key behaviour:

- Blur radius resolution is authoritative in the Blur node.
- Regional support asks the Blur node for its exact required sample radius.
- Blur processes the complete supplied storage including scheduler guard bands.
- Full-world edge clamping is preserved.

### Pass C - Bounded neighbourhood operators

Commit: `82e7ff9` - `Regionalize bounded neighbourhood operators`

Regionalised:

- Slope
- Curvature
- Angle
- Sharpen
- Denoise

Added shared regional field-sampling support and path-aware dependency accumulation.

The project owner reported this checkpoint compiled and tested successfully in UE 5.8.

### Pass D - Mountain global semantics

Commit: `7eb836e` - `Make regional Mountain semantics globally exact`

Key behaviour:

- Terrain Context uses physical spacing from the full reference world when evaluating a region.
- Regional semantic derivation can consume the exact whole-world Height minimum/maximum.
- Mountain Basic Bulk Low/Medium/High uses exact global maximum semantics.
- Mountain core range is reduced from the virtual full-world core without allocating the full raster.
- Internal region derivatives use scheduler guard samples while actual full-world boundaries retain one-sided/clamped behaviour.

The project owner reported this Mountain pass compiled successfully.

Still deliberately unsupported:

- regional Mountain with connected `In` multiplier, because exact extrema of `MountainCore * In` require paired upstream evaluation; independent extrema are mathematically insufficient.
- regional Mountain semantic scalar outputs feeding later neighbourhood nodes, because processed semantic guard bands are not yet published as a complete downstream contract.
- erosion-backed Mountain styles, because they require cached/global process state rather than independent local simulation.

### Pass E - Exact AutoLevel global summaries

Commit: `27e3ba7` - `Enable exact regional AutoLevel summaries`

Key additions:

- generic `FEonformTerrainEvaluator::EvaluateOutput(...)`
- active recipe access in evaluation context
- `FEonformTerrainGlobalSummary::ResolveOutputRange(...)`
- bounded full-width strip reduction of a reachable upstream graph branch
- cached exact minimum/maximum keyed by recipe/output/reference context
- authoritative AutoLevel range application remains in the AutoLevel node
- regional AutoLevel is supported when its `Input` is connected and the upstream branch is region-equivalent

Global summary strips use scheduler halo derived by `FEonformTerrainRegionalSupport::Analyze()` and scan only strip interiors into the reduction.

AutoLevel semantics:

```text
Terrain     -> [-1, +1]
ScalarField -> [ 0, +1]
```

Degenerate source range preserves input rather than inventing a remap.

Equalize remains unsupported regionally because exact rank mapping requires the whole-world value distribution/order statistics, not merely min/max.

## 4. Current optimisation pass - bounded Mountain range strips

Status: IMPLEMENTED IN THE CURRENT CHANGESET; REQUIRES OWNER BUILD/TEST AFTER PULL.

### Problem

`EonformMountainRegional::ResolveCoreRange()` was exact and bounded-memory, but it streamed only two reference rows per `GenerateCore()` call.

For a 4097-row reference world this means roughly 2049 expensive Mountain/Ridge range-evaluation calls before the range is cached.

The result is correct but unnecessarily call-heavy.

### Change

The Mountain core reducer now uses the same bounded full-width strip budget as the generic global-summary infrastructure.

Shared policy:

```cpp
FEonformTerrainGlobalSummary::PreferredStripRows = 32;
```

This value is explicitly a scheduling/memory budget, not a terrain calibration constant. Changing it may alter memory/performance but must not alter terrain values.

For a 4097-row world, 32-row strips reduce the number of Mountain `GenerateCore()` calls from approximately 2049 to approximately 129 while still visiting the same reference samples.

### Exactness

No Mountain algorithm changed.

Each strip:

1. is full-width,
2. begins/ends on exact reference-lattice rows,
3. constructs its world bounds from `ReferenceDomain.InteriorSampleToWorld(...)`,
4. calls the existing authoritative regional `GenerateCore()`,
5. reduces every interior sample into the same min/max,
6. overlaps one preceding row only when the final remainder is a single row because a valid grid domain needs at least two rows.

The duplicated final overlap cannot change min/max.

### Files changed in this pass

`Plugins/Eonform/Source/EonformCore/Public/EonformTerrainGlobalSummary.h`

- Added shared `PreferredStripRows` scheduling budget.
- Documented that it must not affect numerical semantics.

`Plugins/Eonform/Source/EonformCore/Private/EonformTerrainGlobalSummary.cpp`

- Removed the private duplicate `PreferredStripRows` constant.
- Generic global summaries now consume the shared infrastructure budget.

`Plugins/Eonform/Source/EonformCore/Private/EonformMountainRegional.cpp`

- Uses `FEonformTerrainGlobalSummary::PreferredStripRows`.
- Replaced two-row range streaming with bounded multi-row strip streaming.
- Mountain still calls its existing `GenerateCore()`; no private substitute implementation was introduced.

`Plugins/Eonform/Source/EonformCore/Private/EonformMountainRegional.h`

- Updated range-reducer contract documentation from two-row streaming to bounded full-width strips.

`Docs/REGIONAL_OPTIMISATION_HANDOFF.md`

- This durable restart document.

## 5. Current regional support matrix

### Supported/proven core classes

Direct/local examples currently admitted by regional support include:

- Perlin Noise
- Voronoi
- Constant
- Combine
- Clamp
- Invert
- Threshold
- Terrace
- Blur
- Denoise
- Sharpen
- Slope
- Curvature
- Angle
- Ridge
- Basic Mountain under the restrictions below
- AutoLevel with connected region-equivalent upstream input

### Explicitly blocked

Equalize:

```text
requires exact whole-world ranks/distribution
```

Hydrology family:

- Hydraulic Erosion
- Rivers
- Flow Map
- Flow Map Classic

Reason:

```text
requires macro/global drainage/process state before independent regional refinement
```

Mountain:

- non-Basic erosion-backed styles are blocked
- connected `In` is blocked until paired whole-world summary/evaluation exists
- connected semantic outputs are blocked until semantic guard-band publication is complete

Unknown/unproven node types remain blocked by default.

## 6. Global summary cache contract

`FEonformTerrainGlobalSummaryCache` is shared between region evaluations in one generation context.

Generic graph-output range keys include:

- deterministic recipe hash
- node id
- output name
- full reference resolution
- cache context revision
- summary kind (minimum/maximum)

Mountain also uses stable node/layer summary keys for Ridge and Mountain core ranges.

Do not reuse a cached scalar across a changed recipe/reference context unless the key includes every semantic dependency.

## 7. Terrain View / streaming direction

The next large performance architecture should build on regional exactness rather than returning to a monolithic raster.

Recommended progression:

1. **Region identity and cache records**
   - deterministic region key
   - reference bounds/resolution
   - recipe hash/revision
   - available quality/detail level
   - cached fields/derived data metadata

2. **Low-resolution whole-world representation**
   - enough for Terrain View and distant planning
   - must not imply high-detail Mesh Terrain objects exist everywhere

3. **Demand-driven high-resolution evaluation**
   - camera/player/editor requests regions
   - regional evaluator produces exact requested fields
   - shared global summaries are reused

4. **Materialisation boundary**
   - evaluated data is distinct from Mesh Terrain materialisation
   - instantiate/update Mesh Terrain only where required

5. **Lifecycle**
   - high-detail region may be dematerialised while cheap evaluated/cache state remains
   - collision, PCG and gameplay activation are separate concerns

6. **Scheduling**
   - prioritise player/camera proximity and editor viewport demand
   - avoid redundant evaluation of the same recipe/revision/region/detail key

## 8. High-value remaining optimisation work

### 8.1 Mountain private radial helper authority debt

`EonformMountainRegional.cpp` currently contains a private `RadialMultiplier(...)` helper while full-world Mountain calls `EonformTerrainProceduralOps::ApplyRadialGradientMultiply(...)`.

This predates the strict node-authority rule.

Future work should expose a shared authoritative radial sample operation from the radial operation/node implementation and have both full-world and regional Mountain call it. Do not merely copy the private formula elsewhere.

### 8.2 Connected Mountain `In`

To admit this exactly, stream/evaluate the paired Mountain core and upstream multiplier over the same reference coordinates and reduce the actual product.

Do not derive product extrema from independent min/max values.

### 8.3 Mountain semantic downstream halos

Regional Mountain currently needs one local border for Terrain Context derivatives, but semantic outputs are not yet a complete processed guard-band product for arbitrary downstream neighbourhood chains.

A future pass should define whether semantic fields retain/rebuild storage guard values and prove seam equivalence before removing this block.

### 8.4 Equalize

Exact regional Equalize needs order statistics.

Valid directions include:

- exact full value list (simple but O(N) memory),
- bounded external/spooled sort,
- deterministic external merge of sortable value/sample records,
- another exact order-statistic structure.

A fixed histogram is an approximation and must not silently replace existing Equalize semantics.

### 8.5 Hydrology / erosion

Hydraulic erosion and drainage are process-state problems, not simple halo problems.

Likely architecture:

```text
coarse/global drainage/process solve
        +
regional refinement with boundary/global state
```

Do not independently erode adjacent regions and hope seams match.

## 9. Vertical datum / bathymetry work still pending

This is important but is not part of the current optimisation commit.

Existing physical metrics already contain:

- world width/depth in metres
- elevation scale in metres
- sea level in metres

The intended vertical contract is:

```text
absolute elevation metres = SeaLevelMeters + normalizedHeight * ElevationScaleMeters
world Z metres relative to sea datum = normalizedHeight * ElevationScaleMeters
```

Therefore graph Height `0` represents sea level and should materialise at world Z `0`; negative normalized Height is real bathymetry and positive Height is land above the sea datum.

A previously identified editor bug must still be fixed: the output editor state has a configurable sea-level value but `PublishPhysicalContext()` was observed publishing a hardcoded `0.0` instead of the configured value.

Recommended explicit elevation policy remains:

```text
Unspecified
Above Sea Level Only
Bathymetric
```

`Above Sea Level Only` should reject negative terrain rather than clamp/lift it. `Bathymetric` explicitly accepts negative world-relative elevation. `Unspecified` preserves existing graph compatibility.

No completed disk heightmap exporter was found on this branch at the time of the audit. Establish the datum/encoding contract before implementing one.

## 10. Build/test discipline

The GitHub connector cannot compile the Unreal project locally.

After each pushed optimisation pass the project owner should:

1. pull the active branch,
2. cold-build `EonformEditor` / Development Editor / Win64 when Core/UHT-sensitive changes warrant it,
3. run relevant EONFORM automation tests,
4. exercise full-world vs regional seam/equivalence cases,
5. report compile/test failures before the next semantic expansion.

Do not mark a pass "compiled" in this document until the project owner reports that result.

## 11. Git discipline

Avoid connector-generated multi-commit history for one logical pass.

Preferred pattern:

1. read current branch head/tree,
2. create blobs/tree for all files,
3. create one coherent commit using the previous branch head as its parent,
4. move the branch once,
5. compare old head to new head,
6. inspect commit status.

Do not use external-product comparison language in commit messages.

If local and remote histories diverge, inspect `git status` and `git log --left-right` before merge/rebase/reset/stash. Unreal `.uasset` and ExternalActor changes must be treated as real local work until proven otherwise.

## 12. Immediate next step after this pass

After the owner confirms the bounded Mountain-strip pass compiles/tests, continue optimisation in this order unless profiling proves a more urgent bottleneck:

1. remove Mountain radial-authority duplication,
2. add deterministic region-cache identity/state,
3. avoid repeated evaluation/materialisation for unchanged region keys,
4. establish low-resolution Terrain View world state,
5. separate high-detail evaluation from Mesh Terrain materialisation/lifecycle,
6. then tackle exact connected-Mountain summaries / semantic downstream halos,
7. design global hydrology process state,
8. design exact Equalize order-statistic support.

The governing rule remains: **make less terrain live at once, not less-correct terrain.**
