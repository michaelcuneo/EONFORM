# EONFORM Node Coverage and Implementation Order

This document tracks EONFORM against the current public Gaea 2 node reference and defines the implementation dependency order for the graph runtime.

Reference catalogue: https://docs.gaea.app/reference/nodes/

## Architectural rule

EONFORM Terrain nodes are **high-level landform composites and must be implemented last**.

A Terrain node such as `Mountain` must not be implemented as a single mountain-shaped mathematical lump. The public Gaea documentation describes Mountain as using a modulated Voronoi pattern and distortions, with styles that add erosion, age, alpine character, and strata. EONFORM should follow the same architectural principle: Terrain nodes orchestrate lower-level graph operations and physical process engines already available as Primitive, Modify, Surface, Simulate, Derive, and Utility functionality.

The dependency order is therefore:

```text
Primitive
   -> Modify
   -> Surface
   -> Simulate
   -> Derive / Utility
   -> Terrain composites LAST
   -> EONFORM Terrain Output
   -> UE 5.8 Mesh Terrain adapter
```

A high-level Terrain node may use internal compiled/composite execution for performance, but that implementation must call the same reusable terrain operations exposed by public lower-level nodes wherever practical. We do not maintain a secret second terrain generator beside the graph.

## Status legend

- COMPLETE: public node name exists, descriptor is registered, and a runtime evaluator exists.
- PARTIAL: functional evaluator exists but current Gaea fidelity/parameter coverage still needs hardening.
- MISSING: no public descriptor/evaluator yet.
- EONFORM: intentionally replaced by a native EONFORM/UE workflow.

## Primitive - 23 / 23 represented

Current public Gaea Primitive catalogue:

Cellular, Cellular3D, Cone, Constant, Cracks, DotNoise, Draw, DriftNoise, File, Gabor, Hemisphere, LinearGradient, LineNoise, MultiFractal, Noise, Object, Pattern, Perlin, RadialGradient, Shape, TileInput, Voronoi, WaveShine.

Status: COMPLETE by public node coverage. Individual fidelity passes remain allowed, but missing-node breadth is not the blocker here.

## Modify - 39 / 39 represented

Current public Gaea Modify catalogue is represented end-to-end:

Adjust, Aperture, Autolevel, BlobRemover, Blur, Clamp, Clip, Curve, Deflate, Denoise, Dilate, DirectionalWarp, Distance, Equalize, Extend, Filter, Flip, Fold, GraphicEQ, Heal, Match, Median, Meshify, Origami, Pixelate, Recurve, Shaper, Sharpen, SlopeBlur, SlopeWarp, SoftClip, Swirl, ThermalShaper, Threshold, Transform, Transpose, VariableBlur, Warp, Whorl.

Status: COMPLETE by public node coverage; several implementations still need exact-behaviour/fidelity hardening.

## Surface - 21 / 21 represented

All current public Surface node names now have runtime graph descriptors and evaluators:

Bomber, Bulbous, Contours, Craggy, Distress, FractalTerraces, Grid, GroundTexture, Outcrops, Pockmarks, RockNoise, Rockscape, Roughen, Sand, Sandstone, Shatter, Shear, Steps, Stones, Stratify, Terraces.

The Surface implementations share reusable deterministic operations for scatter/stamping, layered noise, ridged rock breakup, cellular crack structure, strata, slope/curvature bias, directional sand, and spatial resampling. These operations are intended to become building blocks for later Terrain composites rather than duplicated secret algorithms.

`Eonform.Core.Graph.SurfaceNodeCoverage` enforces that all 21 Surface nodes have descriptors and runtime evaluators, can evaluate from a real Perlin terrain source, publish a valid Height field, and contain finite samples.

Surface fidelity remains an iterative product task: coverage here means the nodes are real executable EONFORM operations, not a claim of byte-for-byte equivalence with QuadSpinner's proprietary implementations.

## Simulate - 12 / 26 represented

Existing physical/core nodes:

- Erosion - COMPLETE / physical-scale aware
- Thermal - COMPLETE
- HydroFix - COMPLETE foundation; physical D8 drainage continuity/downcutting
- Rivers - COMPLETE foundation; physical catchment-driven network, optional Headwaters mask, channel/valley carving, River and RiverDepth outputs
- Sediments - COMPLETE foundation; low-slope/concavity-biased sediment accumulation and Deposits output
- Debris - COMPLETE foundation; loose-rock terrain accumulation and Debris output
- Scree - COMPLETE foundation; slope/edge-biased scree accumulation and Scree output

Erosion-evolution batch now represented:

- EasyErosion - COMPLETE foundation; current published style list mapped onto the shared physical hydraulic solver with Influence, directional bias and deterministic Seed controls
- Erosion2 - COMPLETE foundation; advanced physical hydraulic pass with Duration, Downcutting, Erosion Scale, suspended/bed/coarse sediment classes, deposition boosts, shape controls and optional directional/orographic rainfall
- Thermal2 - COMPLETE foundation; physical-scale thermal erosion with Duration, Strength, Anisotropy, talus Angle, Sediment Removal and Feature Scale in metres
- Crumble - COMPLETE foundation; edge/crevice/flow-sensitive terrain collapse with horizontal/vertical bias, hardness, Edge, Downcutting, Depth and directional controls
- Hillify - COMPLETE foundation; deterministic hill-form creep with Coverage, Moderate/Aggressive creep and Smooth/Eroded surface modes

The `Rivers` implementation deliberately sits on EONFORM's physical hydrology rather than introducing a parallel river solver. Catchment area, drainage direction, distance-to-outlet, physical elevation scale and world dimensions are the reusable engine beneath the graph node.

`Eonform.Core.Graph.SimulateFoundationChain` evaluates:

```text
Perlin -> HydroFix -> Rivers -> Sediments -> Debris -> Scree
```

`Eonform.Core.Graph.SimulateEvolutionChain` evaluates:

```text
Perlin -> EasyErosion -> Erosion2 -> Thermal2 -> Crumble -> Hillify
```

and verifies the final terrain plus hydraulic Wear/Deposits/Flow, Thermal2 Talus, Crumble and Hillify fields remain valid and finite.

Still missing from the currently tracked Simulate roadmap:

- Anastomosis
- Dusting
- Glacier
- IceFloe
- Lake
- Lichtenberg
- Shrubs
- Snow
- Snowfield
- Trees
- Sea
- Wizard
- Wizard2

This remains the largest geomorphology gap. EONFORM's physical hydrology, catchment area, stream order, terrain context, geology, erosion, deposition and sea-level datum are engine primitives beneath these graph nodes; they do not replace the nodes.

Next Simulate priority:

1. Lake
2. Sea
3. Snow / Snowfield / Glacier / IceFloe / Dusting
4. Anastomosis / Lichtenberg
5. Trees / Shrubs
6. Wizard / Wizard2 as high-level simulation composites

## Derive - 4 / 14 represented before expansion

Existing:

- Angle - COMPLETE
- Curvature - COMPLETE
- Height - COMPLETE
- Slope - COMPLETE

Missing:

- ColorThreshold
- FlowMap
- FlowMapClassic
- Normals
- Occlusion
- Peaks
- RockMap
- Soil
- TextureBase
- Texturizer

Priority for terrain construction and later Mesh Terrain channels:

1. FlowMap
2. FlowMapClassic
3. Peaks
4. RockMap
5. Soil
6. Normals
7. Occlusion
8. TextureBase / Texturizer
9. ColorThreshold when color-field workflows are expanded

## Utility - 2 / 19 represented before expansion

Existing:

- Combine - COMPLETE
- Edge - PARTIAL (`ZeroBorders` implementation presented publicly as Edge)

Missing:

Accumulator, Chokepoint, Compare, DataExtractor, Gate, Layers, LoopBegin, MacroPort, LoopEnd, Mask, Math, Mixer, Repeat, Reseed, Route, Seamless, Switch, Var.

Utility work should be split into data/mask operations first, graph-control nodes second, and loop semantics only after recipe execution supports explicit loop scopes safely.

## Terrain - 0 / 14 public nodes; intentionally LAST

The hidden legacy `TerrainShape` node is not counted as a public Terrain node and should eventually be removed/replaced by real composites.

Public Terrain catalogue:

Canyon, Crater, CraterField, DuneSea, Island, Mountain, MountainRange, MountainSide, Plates, Ridge, Rugged, Slump, Uplift, Volcano.

All remain intentionally deferred until their lower-level dependencies exist.

### Composite principle example: Mountain

The public Gaea documentation states that Mountain uses a **modulated Voronoi pattern and distortions**. EONFORM's final Mountain implementation should therefore be an authored/composable landform recipe conceptually similar to:

```text
Voronoi / Cellular structure
        -> profile shaping
        -> spatial distortion / warp
        -> bulk / footprint control
        -> ridge and peak shaping
        -> style branch
             Basic
             Eroded -> erosion operations
             Old    -> erosion + smoothing/weathering
             Alpine -> ridge/crag/scree-style operations
             Strata -> stratification/sandstone-style operations
        -> final macro controls
```

The user sees one `Mountain` node with macro parameters. Internally the node coordinates reusable lower-level operations. The precise implementation is EONFORM's own and should exploit physical scale, geology and Mesh Terrain where useful rather than merely reproducing a screenshot of another product.

## Colorize - later product phase

Current public family contains CLUTer, ColorErosion, Gamma, HSL, RGBMerge, RGBSplit, SatMap, Splat, SuperColor, Synth, Tint, WaterColor, Weathering.

These matter for materials/texture workflows but do not block physical terrain construction. Implement after core geometry/process/derive coverage, or alongside Mesh Terrain weight-channel work where overlap is useful.

## Output - EONFORM-specific strategy

EONFORM does not need to reproduce Gaea's Unity/Unreal handoff nodes literally. `Terrain Output -> UE 5.8 Mesh Terrain` is the native product endpoint.

Useful general output concepts such as AO, TextureBaker, Mesher, PointCloud, Shade, Sunlight and Cartography can be evaluated later. Engine-specific output remains EONFORM-owned.

## Completion gates before Terrain composites

Before implementing public Terrain nodes, the following minimum capabilities should be available as reusable operations:

- all Primitive nodes
- all Modify nodes
- complete Surface family
- physical hydrology / drainage
- Rivers, HydroFix, Sediments, Scree/Debris
- erosion and thermal variants needed by terrain styles
- FlowMap, Peaks, RockMap, Soil and physical slope/curvature
- sufficient Utility mask/combine/math support for composite recipes

Only then should Terrain nodes become public. This ensures `Mountain`, `Island`, `Canyon`, `Volcano`, etc. are realistic landform systems rather than decorative generators.
