# EONFORM Implementation Status

## Branches

- Protected baseline: `agent/terrain-foundation` at `439a8c14ab7b346ca8758223da2a3a2db66a650b`
- Active architecture branch: `agent/mesh-terrain-foundation`

The protected baseline must remain unchanged.

## Verified checkpoints

Compiled and exercised successfully in Unreal Engine 5.8 by the project owner:

- commercial plugin/module boundary
- `FEonformGridDomain` / `FEonformScalarField`
- shared-backed legacy `FTerrainHeightField`
- semantic context/process/geology fields
- `FEonformTerrainDataset` and canonical field names
- runtime `FEonformTerrainDatasetRegistry`
- first visible `Tools -> EONFORM` dataset inspector
- first-class hydraulic erosion Height/Wear/Deposits/Flow outputs
- runtime-safe recipe/evaluator foundation
- first professional GraphEditor surface
- editable graph authoring with node creation, rewiring, parameter editing, recipe reconstruction, and cycle prevention

The project owner has run these runtime graph automation tests successfully:

- `Eonform.Core.Graph.RecipeValidation`
- `Eonform.Core.Graph.SourceToHydraulicErosion`
- `Eonform.Core.Graph.CycleDetection`

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

The recipe/evaluator therefore remains runtime-safe and editor-independent. Orakai will apply a constrained generation policy before evaluation rather than exposing unrestricted professional terrain authoring. See `Docs/ARCHITECTURE.md` and `Docs/ORAKAI_GENERATION_POLICY.md`.

## Verified runtime graph foundation

Hydraulic erosion is Core-owned and the legacy project API delegates to the same implementation. `FEonformTerrainEvaluator` consumes `FEonformTerrainRecipe`, resolves dependencies, detects cycles, memoizes one graph run, invokes runtime node evaluators, and returns `FEonformTerrainDataset`.

The first proven runtime graph remains:

```text
SourceDataset -> HydraulicErosion
```

with Height, Wear, Deposits, and Flow outputs.

Runtime node descriptors provide the shared metadata contract for professional editor authoring and future constrained Orakai authoring.

## Verified editable graph authoring

`EonformEditor` uses Unreal's native GraphEditor stack. The current workbench supports:

- descriptor-driven right-click node creation
- normal terrain pin wiring/rewiring
- one terrain connection per input
- connection-time cycle rejection
- stable recipe node ids
- descriptor-backed Hydraulic Erosion parameter editing
- multiple chained Hydraulic Erosion nodes
- live editor graph -> runtime recipe reconstruction on evaluation
- exactly-one-terminal-output validation
- evaluated dataset publication to the existing field inspector

The editor graph still performs no terrain simulation itself. Runtime evaluation remains authoritative.

## Current checkpoint: saveable graph assets

This checkpoint adds persistent EONFORM graph assets without introducing a second execution model.

### Serializable runtime recipe

`FEonformTerrainNode`, `FEonformTerrainConnection`, and `FEonformTerrainRecipe` are now Unreal-reflected serializable structs in `EonformCore`.

The same recipe type is therefore suitable for:

- editor asset persistence
- cooked runtime assets
- deterministic graph evaluation
- future Orakai island recipe storage/validation

`EonformCore` now depends on `CoreUObject` only to support reflection/serialization; it remains editor-independent.

### `UEonformTerrainGraphAsset`

`EonformRuntime` now provides a `UDataAsset`-based graph asset containing:

- the exact runtime `FEonformTerrainRecipe`
- editor node layout metadata keyed by stable recipe node id

Editor positions are authoring metadata only. Runtime execution consumes the persisted recipe.

### Workbench New / Open / Save

The EONFORM graph toolbar now contains:

```text
New   Open   Save   Evaluate Graph
```

Behavior:

- `New` uses Unreal AssetTools to create a real graph asset in the Content Browser, then initializes the default Source Dataset -> Hydraulic Erosion graph and saves it.
- `Open` uses Unreal's modal Content Browser asset picker filtered to `UEonformTerrainGraphAsset`, then reconstructs a fresh transient editor graph from the persisted recipe and layout.
- `Save` serializes the current live nodes, typed parameters, connections, terminal output, and node positions back into the current asset.
- `Save` on an unsaved graph acts as Save As and opens the normal Unreal asset creation dialog.
- package saving uses Unreal's normal checkout/save path so source-control behavior remains conventional.

Opening an asset rebuilds the native `SGraphEditor` around a new transient `UEdGraph`; the `.uasset` stores recipe/layout data rather than serializing editor widget objects.

### Asset factory

`UEonformTerrainGraphAssetFactory` creates graph assets through Unreal's normal asset creation system. The workbench drives it directly; a dedicated double-click asset editor is intentionally deferred until the persistence round-trip is proven.

## Automated coverage

Existing Core graph tests remain in place, including:

```text
Eonform.Core.Graph.NodeDescriptors
```

A new Runtime asset-model test is added:

```text
Eonform.Runtime.GraphAsset.Model
```

It verifies that a graph asset owns a valid runtime recipe, stable node ids/parameters, and one updateable layout record per node.

## Validation required before next step

This checkpoint changes Core UHT/reflection state, adds a Runtime `UDataAsset`, adds an editor `UFactory`, and adds AssetTools/ContentBrowser integration. Use a cold build rather than Live Coding.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `EonformEditor` / Development Editor / Win64.
4. Open `Tools -> EONFORM`.
5. Confirm the toolbar shows `New`, `Open`, `Save`, and `Evaluate Graph`.
6. Edit the graph: add a second Hydraulic Erosion node, wire it, change parameters, and move the nodes to obvious positions.
7. Press `Save`. For an unsaved graph, choose an asset name/path in the normal Unreal dialog.
8. Confirm the graph asset appears in the Content Browser.
9. Change the graph after saving, then press `Open` and choose the saved asset.
10. Confirm node ids/topology, parameter values, and node positions return to the saved state.
11. Evaluate the reopened graph and confirm the recipe hash/output are consistent with the saved graph.
12. Close/reopen the EONFORM tool and use `Open` again to confirm persistence is independent of the transient editor canvas.
13. Run `Eonform.Runtime.GraphAsset.Model` if convenient.
14. Confirm the existing terrain actor remains visually unchanged.

## Next implementation step

Once asset persistence is verified, begin replacing the legacy monolithic terrain pipeline with additional descriptor-backed Core nodes.

The next node migration should establish reusable foundational generation nodes before expanding breadth. Candidate sequence:

1. base/noise terrain source node
2. shaping/mountain node
3. thermal erosion migration into Core
4. context/derive node producing Elevation/Slope/Curvature/regional fields
5. geology node
6. hydrology node
7. runtime Dynamic Mesh materialization node/orchestrator

At that point a saved EONFORM asset can become the actual source of a complete generated island rather than a graph layered on top of the legacy actor dataset.
