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
- first visible professional graph UI with `Source Dataset -> Hydraulic Erosion`

The following runtime graph automation tests have been run successfully by the project owner:

- `CodenameGaea.Core.Graph.RecipeValidation`
- `CodenameGaea.Core.Graph.SourceToHydraulicErosion`
- `CodenameGaea.Core.Graph.CycleDetection`

This establishes the runtime recipe/evaluator as a verified boundary beneath editor authoring.

## Runtime island requirement

Orakai players will create their own islands in a packaged game and then play on them.

The recipe/evaluator therefore remains runtime-safe and editor-independent. Orakai will apply a constrained generation policy before evaluation rather than exposing unrestricted professional terrain authoring. See `Docs/ARCHITECTURE.md` and `Docs/ORAKAI_GENERATION_POLICY.md`.

## Verified runtime graph foundation

Hydraulic erosion is Core-owned and the legacy project API delegates to the same implementation. `FGaeaTerrainRecipe` remains plain runtime-safe data with stable node ids/types, typed parameter maps, and named connections. `FGaeaTerrainEvaluator` validates recipes, resolves dependencies, detects cycles, memoizes a graph run, invokes runtime node evaluators, and returns `FGaeaTerrainDataset`.

The first proven runtime graph remains:

```text
SourceDataset -> HydraulicErosion
```

with Height, Wear, Deposits, and Flow outputs.

## Verified first graph UI

`CodenameGaeaEditor` uses Unreal's `GraphEditor` stack through editor-only types:

- `UGaeaEditorGraph`
- `UGaeaEditorGraphNode`
- `UGaeaEditorGraphSchema`
- `SGaeaTerrainGraphPanel`

The first visible graph renders and evaluates the runtime-backed:

```text
[Source Dataset] -> [Hydraulic Erosion]
```

`FGaeaTerrainDatasetSnapshot` carries physical metadata including the real terrain `HeightScale`, so graph evaluation uses the same physical scale as the source actor rather than a hard-coded editor value.

## Current checkpoint: editable graph authoring

This checkpoint turns the previously constrained visual graph into a real authoring surface while keeping the runtime recipe as the execution contract.

### Runtime node descriptors

`CodenameGaeaCore` now provides runtime-safe node metadata through:

- `EGaeaTerrainParameterType`
- `FGaeaTerrainPortDescriptor`
- `FGaeaTerrainParameterDescriptor`
- `FGaeaTerrainNodeDescriptor`
- `FGaeaTerrainNodeDescriptorRegistry`

Built-in descriptors currently cover:

```text
Input / Source Dataset
Simulate / Hydraulic Erosion
```

Descriptors provide stable node type ids, display/category/description metadata, typed input/output ports, parameter defaults, and numeric limits. The professional editor consumes the same metadata that constrained game-facing authoring can later use for policy/range enforcement.

Hydraulic Erosion descriptor parameters:

- Iterations
- Rainfall
- Flow Rate
- Sediment Capacity
- Erosion Rate
- Deposition Rate
- Evaporation
- Minimum Slope

### Right-click node creation

`UGaeaEditorGraphSchema::GetGraphContextActions()` now builds the node palette from the runtime descriptor registry instead of hard-coded editor menu entries.

`Source Dataset` remains a singular protected graph root and is not offered for additional creation. Hydraulic Erosion can be added from the graph context menu.

New nodes receive a new stable recipe `FGuid`, descriptor-defined pins, and descriptor-defined parameter defaults.

### Real connection editing

Terrain pins can now be connected and rewired through the normal Unreal graph canvas.

The schema currently enforces:

- output-to-input direction only
- matching terrain pin category
- no self-connections
- one connection per terrain input
- replacement of an existing input connection
- no connection that would create a dependency cycle

The Core evaluator retains independent cycle detection as a second safety layer.

### Editor graph -> runtime recipe synchronization

The editor graph is now the authoring state for this transient workbench checkpoint.

Every `Evaluate Graph` action reconstructs a fresh `FGaeaTerrainRecipe` from:

- current editor nodes
- current recipe node ids/types
- current typed parameter values
- current pin connections
- current terminal graph topology

The graph must have exactly one terminal output node. Disconnected branches therefore produce a clear invalid-graph error instead of being silently ignored.

The reconstructed recipe is passed to the already-verified runtime `FGaeaTerrainEvaluator`. The editor graph itself still performs no terrain simulation.

### Selected-node parameter panel

Selecting a single terrain node now populates a parameter panel beside the graph.

The panel is generated from descriptor metadata and currently supports:

- floating-point parameters
- integer parameters
- boolean parameters
- name parameters
- declared min/max ranges

Editing Hydraulic Erosion values changes the parameter maps that are copied into the runtime recipe on the next evaluation.

### Source-node invariants

The graph always starts with one `Source Dataset` node.

For this checkpoint it:

- cannot be deleted
- cannot be duplicated
- is omitted from the node-creation palette

This prevents accidental multiple external roots until multi-source semantics are designed deliberately.

## Automated coverage

Existing Core graph tests remain in place.

A new test is added:

```text
CodenameGaea.Core.Graph.NodeDescriptors
```

It verifies built-in descriptor availability, Source Dataset port shape, Hydraulic Erosion port shape, parameter count, and the Iterations parameter type/default/range.

## Validation required before next step

This checkpoint changes UHT-generated editor node state, adds public Core descriptor types and implementations, and substantially changes GraphEditor interaction. Use a cold build rather than Live Coding.

1. Pull `agent/mesh-terrain-foundation`.
2. Close Unreal Editor.
3. Build `CodenameGaeaEditor` / Development Editor / Win64.
4. Open Unreal and regenerate the current terrain actor.
5. Open `Tools -> Codename Gaea`.
6. Confirm the default `Source Dataset -> Hydraulic Erosion` graph still appears.
7. Select Hydraulic Erosion and confirm all eight parameters appear in the right-side parameter panel.
8. Change `Iterations` or `Rainfall`, click `Evaluate Graph`, and confirm evaluation succeeds and produces a different recipe hash when the value changes.
9. Right-click empty graph space and add another `Hydraulic Erosion` node from the `Simulate` category.
10. Rewire the graph into `Source Dataset -> Hydraulic Erosion -> Hydraulic Erosion` and evaluate successfully.
11. Leave the new node disconnected and confirm evaluation reports that the graph has more than one terminal output rather than silently ignoring the branch.
12. Try to wire a downstream node back into an upstream node and confirm the editor rejects the cycle.
13. Run `CodenameGaea.Core.Graph.NodeDescriptors` if convenient.
14. Confirm the field inspector still refreshes to `CodenameGaeaGraph` after successful evaluation.
15. Confirm the existing terrain actor remains visually unchanged; graph evaluation still publishes data for inspection and does not yet replace the actor mesh.

## Next implementation step

After this editable-authoring checkpoint is verified, introduce persistence without changing the runtime execution model:

1. create a saveable Codename Gaea graph/recipe asset
2. serialize runtime-safe recipe data plus editor-only node positions
3. load/save the editor canvas from that asset
4. add New/Open/Save workflow in the Codename Gaea workbench
5. preserve stable recipe ids and deterministic hashes across editor sessions
6. then begin migrating additional legacy terrain stages into descriptor-backed runtime nodes

The asset will store authoring data; `FGaeaTerrainRecipe`/`FGaeaTerrainEvaluator` remain the execution model for both the commercial plugin and constrained Orakai runtime island generation.
