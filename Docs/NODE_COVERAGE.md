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

- Cellular - COMPLETE
- Cellular3D - COMPLETE
- Cone - COMPLETE
- Constant - COMPLETE
- Cracks - COMPLETE
- DotNoise - COMPLETE
- Draw - COMPLETE
- DriftNoise - COMPLETE
- File - COMPLETE
- Gabor - COMPLETE
- Hemisphere - COMPLETE
- LinearGradient - COMPLETE
- LineNoise - COMPLETE
- MultiFractal - COMPLETE
- Noise - COMPLETE
- Object - COMPLETE
- Pattern - COMPLETE
- Perlin - COMPLETE
- RadialGradient - COMPLETE
- Shape - COMPLETE
- TileInput - COMPLETE
- Voronoi - COMPLETE
- WaveShine - COMPLETE

Primitive coverage is sufficient to support later composite Terrain nodes. Individual fidelity passes remain allowed, but missing-node breadth is not the blocker here.

## Modify - 39 / 39 represented

Current public Gaea Modify catalogue is represented end-to-end:

Adjust, Aperture, Autolevel, BlobRemover, Blur, Clamp, Clip, Curve, Deflate, Denoise, Dilate, DirectionalWarp, Distance, Equalize, Extend, Filter, Flip, Fold, GraphicEQ, Heal, Match, Median, Meshify, Origami, Pixelate, Recurve, Shaper, Sharpen, SlopeBlur, SlopeWarp, SoftClip, Swirl, ThermalShaper, Threshold, Transform, Transpose, VariableBlur, Warp, Whorl.

Status: COMPLETE by public node coverage; several implementations still need exact-behaviour/fidelity hardening.

## Surface - 21 / 21 represented

All current public Surface node names now have runtime graph descriptors and evaluators:

- Bomber - COMPLETE foundation
- Bulbous - COMPLETE foundation
- Contours - COMPLETE foundation
- Craggy - COMPLETE foundation
- Distress - COMPLETE foundation
- FractalTerraces - COMPLETE
- Grid - COMPLETE foundation
- GroundTexture - COMPLETE foundation
- Outcrops - COMPLETE foundation
- Pockmarks - COMPLETE foundation
- RockNoise - COMPLETE foundation
- Rockscape - COMPLETE foundation
- Roughen - COMPLETE foundation
- Sand - COMPLETE foundation
- Sandstone - COMPLETE foundation
- Shatter - COMPLETE foundation
- Shear - COMPLETE foundation
- Steps - COMPLETE foundation
- Stones - COMPLETE foundation
- Stratify - COMPLETE foundation
- Terraces - COMPLETE

The new Surface implementations share reusable deterministic operations for scatter/stamping, layered noise, ridged rock breakup, cellular crack structure, strata, slope/curvature bias, directional sand, and spatial resampling. These operations are intended to become building blocks for later Terrain composites rather than duplicated secret algorithms.

`CodenameGaea.Core.Graph.SurfaceNodeCoverage` enforces that all 21 Surface nodes have descriptors and runtime evaluators, can evaluate from a real Perlin terrain source, publish a valid Height field, and contain finite samples.

Surface fidelity remains an iterative product task: coverage here means the nodes are real executable EONFORM operations, not a claim of byte-for-byte equivalence with QuadSpinner's proprietary implementations.

## Simulate - 2 / 26 represented before expansion

Existing:

- Erosion - COMPLETE / physical-scale aware
- Thermal - COMPLETE

Missing:

- Anastomosis
- Crumble
- Debris
- Dusting
- EasyErosion
- Erosion2
- Glacier
- Hillify
- HydroFix
- IceFloe
- Lake
- Lichtenberg
- Rivers
- Scree
- Sea
- Sediments
- Shrubs
- Snow
- Snowfield
- Thermal2
- Trees
- Wizard
- Wizard2

This is the largest geomorphology gap. EONFORM's physical hydrology, catchment area, stream order, terrain context, geology, erosion, deposition and sea-level datum are engine primitives beneath these graph nodes; they do not replace the nodes.

Implementation priority inside Simulate:

1. HydroFix
2. Rivers
3. Sediments
4. Debris
5. Scree
6. Erosion2 / EasyErosion
7. Thermal2 / Crumble / Hillify
8. Lake
9. Sea
10. Snow / Snowfield / Glacier / IceFloe / Dusting
11. Anastomosis / Lichtenberg
12. Trees / Shrubs
13. Wizard / Wizard2 as high-level simulation composites

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

- Accumulator
- Chokepoint
- Compare
- DataExtractor
- Gate
- Layers
- LoopBegin
- MacroPort
- LoopEnd
- Mask
- Math
- Mixer
- Repeat
- Reseed
- Route
- Seamless
- Switch
- Var

Utility work should be split into:

- data/mask operations first: Accumulator, Compare, Mask, Math, Mixer, Layers, Seamless, Repeat, Reseed;
- graph-control nodes second: Gate, Route, Switch, Var, MacroPort, DataExtractor;
- loop semantics only after recipe execution supports explicit loop scopes safely.

## Terrain - 0 / 14 public nodes; intentionally LAST

The hidden legacy `TerrainShape` node is not counted as a public Terrain node and should eventually be removed/replaced by real composites.

Public Terrain catalogue:

- Canyon
- Crater
- CraterField
- DuneSea
- Island
- Mountain
- MountainRange
- MountainSide
- Plates
- Ridge
- Rugged
- Slump
- Uplift
- Volcano

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
