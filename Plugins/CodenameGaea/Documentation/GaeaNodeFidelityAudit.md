# EONFORM / Gaea Node Fidelity Audit

This document is the reference-fidelity gate for Gaea-facing EONFORM nodes.

A matching node name is **not** sufficient. A node is `Verified` only when its public ports, property names/options, defaults/ranges where documented, and observable operation agree with the current Gaea 2 reference closely enough that an equivalent graph has the same semantics.

Status meanings:

- **Verified** — contract and behavior reviewed against current Gaea documentation; regression coverage exists or is being added.
- **Corrected** — a material mismatch was found and an audited override has been implemented; local UE compile/tests still required.
- **Contract mismatch** — public controls/ports differ from Gaea.
- **Behavior mismatch** — controls may look similar but the evaluator does something materially different.
- **Unsupported architecture** — current evaluator lacks graph scope/state needed for the documented behavior. The node must not be treated as faithful until the architecture exists.
- **Pending** — not yet cleared by the fidelity audit. Pending nodes must not be used inside new high-level Terrain composites.

## Mountain dependency gate

| Node | Status | Audit finding |
|---|---|---|
| RadialGradient | Corrected | Old evaluator ignored graph target resolution and physical rectangular world dimensions. Audited implementation now uses the evaluation context and documented Scale/Height/X/Y controls. |
| Voronoi | Corrected | Old descriptor advertised 15 controls but evaluator ignored Warp Type/Frequency/Amplitude/Octaves, Scale X/Y and X/Y. Audited implementation now consumes all advertised documented controls and provides distinct C/N/R/P/A/S/M/D forms. |
| Combine | Corrected | Old Mask behavior faded Input1 into the already-combined result. Gaea specifies white mask = Input1 and black mask = Input2. Autolevel was advertised but ignored. Both are corrected in the audited implementation. |
| Cracks | Verified | Multi-octave Voronoi-edge crack field with Style, Octaves, Scale, Depth, Jitter, Warp, Seed and non-uniform scale. Suitable for masked subtractive workflows. |
| Shaper | Verified | Shape, Local Effect/Area and Maintain Fine Details/Detail Size behavior matches the documented role of adding/removing body before erosion while retaining detail. |
| Warp | Contract mismatch / Behavior mismatch | Current public node is only Size/Strength/Seed plus optional Guide and uses a simple smooth-noise displacement. Gaea exposes warp source, Z scale, perturbation, complexity, roughness, normalization, edge behavior, modulation and iterative modes. Do not use in final Mountain until corrected. |
| RockNoise | Contract mismatch / Behavior mismatch | Current implementation is ridged FBM added directly to incoming terrain. Gaea RockNoise is a flat rock-field generator with Size, Variety, Octaves, Seed and Style A/B/C intended for Embed/Insert workflows. |
| GroundTexture | Contract mismatch / Behavior mismatch | Current implementation is generic macro/micro FBM with Strength/Scale/Seed. Gaea uses Method (Harsh/Rocky/Rough), Strength, Coverage and Density. |
| Stratify | Contract mismatch / Behavior mismatch | Current grouped Surface implementation does not expose the documented Spacing, Octaves, Intensity, Shape, Seed, Tilt Amount and Direction contract accurately. |
| Erosion | Pending | Solver has recently been upgraded to drainage-network/stream-power behavior; full public contract and output parity still needs documentation-by-documentation review. |
| Erosion2 | Pending | Advanced solver exists, but every Erosion2 control and secondary output still needs reference validation before Terrain composites depend on it. |
| Thermal | Pending | Physical talus solver exists; public Gaea contract/options still need full fidelity review. |
| Thermal2 | Pending | Physical-scale solver exists; public contract and output masks still need full fidelity review. |
| Mask | Pending | Current post-effect Before/After implementation is directionally appropriate; exact automatic-before semantics and ports need reference validation. |

## Primitive

| Node | Status | Notes |
|---|---|---|
| Cellular | Pending | Extended cellular implementation exists; validate Metric/Distance Function and transform controls. |
| Cellular3D | Pending | Validate 3D cellular controls and output interpretation. |
| Cone | Pending | Validate scale/height/position semantics and graph-resolution behavior. |
| Constant | Pending | Validate Height/Color/Noise output modes against current Gaea. |
| Cracks | Verified | See Mountain dependency gate. |
| DotNoise | Pending | Validate density/distribution controls. |
| Draw | Pending | Editor/authoring semantics require special attention. |
| DriftNoise | Pending | Validate shelves/drift morphology and full property contract. |
| File | Pending | Validate scale modes, transforms, color-vs-height handling and file properties. |
| Gabor | Pending | Validate frequency/orientation/bandwidth semantics. |
| Hemisphere | Pending | Validate Scale/Height/X/Y/Flatten and physical-domain behavior. |
| LinearGradient | Pending | Validate orientation/position controls and physical domain. |
| LineNoise | Pending | Validate Bucket Size, Style, Clamp, Direction and morphology. |
| MultiFractal | Pending | Large descriptor exists; verify every advertised control is consumed by evaluator. |
| Noise | Pending | Validate Random/Perlin/Gaussian/Fixed/Micro and density/blend semantics. |
| Object | Pending | Asset/editor integration and transform semantics need review. |
| Pattern | Pending | Validate pattern types and spatial sizing. |
| Perlin | Pending | Validate Gaea geo-variant rather than generic fractal Perlin only. |
| RadialGradient | Corrected | See Mountain dependency gate. |
| Shape | Pending | Contract is close; validate Thickness, polygon handling and coordinate conventions. |
| TileInput | Pending | Tile/build architecture dependent. |
| Voronoi | Corrected | See Mountain dependency gate. |
| WaveShine | Pending | Validate ripple/wind/light controls. |

## Terrain

High-level Terrain nodes must be composites over verified lower-level nodes. They are deliberately gated until dependencies pass.

| Node | Status | Notes |
|---|---|---|
| Mountain | Blocked by audit | Existing experimental implementation is not accepted as reference-fidelity. Rebuild only after dependency gate is green. |
| Canyon | Not implemented | Required Terrain family node. |
| CraterField | Not implemented | Required Terrain family node. |
| DuneSea | Not implemented | Required Terrain family node. |
| MountainRange | Not implemented | Build after Mountain. |
| Plates | Not implemented | Required Terrain family node. |
| Ridge | Not implemented | Required Terrain family node. |
| Rugged | Not implemented | Required Terrain family node. |
| Slump | Not implemented | Required Terrain family node. |
| Uplift | Not implemented | Required Terrain family node. |
| Crater | Not implemented | Required Terrain family node. |
| Island | Not implemented | Required Terrain family node. |
| MountainSide | Not implemented | Required Terrain family node. |
| Volcano | Not implemented | Required Terrain family node. |

## Modify

| Node | Status | Notes |
|---|---|---|
| Adjust | Pending | Validate range and adjustment controls. |
| Aperture | Pending | Validate morphology and exact controls. |
| Autolevel | Pending | Expected no controls; verify true min/max remap for terrain and masks. |
| BlobRemover | Pending | Validate connected-component/blob behavior. |
| Blur | Pending | Validate radius/strength semantics and edge behavior. |
| Clamp | Pending | Validate Standard/Normalized, Value and Drop behavior. |
| Clip | Pending | Validate clipping modes and AutoClip if current Gaea version exposes it. |
| Curve | Pending | Validate curve representation, fit and relative behavior. |
| Deflate | Pending | Validate morphology. |
| Denoise | Pending | Validate noise removal method and detail preservation. |
| Dilate | Pending | Validate radius/strength/invert/current controls. |
| DirectionalWarp | Pending | Validate strength/direction/edge semantics. |
| Distance | Pending | Validate full distance-transform controls. |
| Equalize | Pending | Validate Factor and histogram behavior. |
| Extend | Pending | Validate range extension semantics. |
| Filter | Pending | Validate filter modes and sizing. |
| Flip | Pending | Validate axes/options. |
| Fold | Pending | Validate fold structure vs current procedural approximation. |
| GraphicEQ | Pending | Validate frequency-band reshaping. |
| Heal | Pending | Validate healing/inpainting behavior. |
| Match | Pending | Validate reference-range matching. |
| Median | Pending | Validate kernel/options. |
| Meshify | Pending | Validate faceting semantics. |
| Origami | Pending | Validate current Gaea contract and morphology. |
| Pixelate | Pending | Validate block sizing and blend. |
| Recurve | Pending | Validate curvature remap controls. |
| Shaper | Verified | See Mountain dependency gate. |
| Sharpen | Pending | Validate amount/radius/edge behavior. |
| SlopeBlur | Pending | Validate slope-direction filtering. |
| SlopeWarp | Pending | Validate local slope vector and normalization controls. |
| SoftClip | Pending | Validate soft clipping curve. |
| Swirl | Pending | Validate center/size/power semantics. |
| ThermalShaper | Pending | Validate talus-aware shaping. |
| Threshold | Pending | Validate threshold output behavior. |
| Transform | Pending | Validate scale/rotation/translation/boundary behavior. |
| Transpose | Pending | Validate Transpose/Embed/Insert semantics and reference input behavior. |
| VariableBlur | Pending | Validate guide-driven blur. |
| Warp | Contract mismatch / Behavior mismatch | See Mountain dependency gate. |
| Whorl | Pending | Validate Stretch/Spin/whorl distribution. |

## Surface

| Node | Status | Notes |
|---|---|---|
| Bomber | Pending | Validate stamp input/grid/jitter/randomization. |
| Bulbous | Pending | Validate geologic inflation behavior. |
| Contours | Pending | Validate cartographic contour semantics. |
| Craggy | Pending | Current generic ridged-FBM modifier needs comparison to Gaea's broken-rock behavior. |
| Distress | Pending | Validate superficial damage controls. |
| FractalTerraces | Pending | Validate full 20-property contract and secondary mask. |
| Grid | Pending | Validate cartographic grid behavior. |
| GroundTexture | Contract mismatch / Behavior mismatch | See Mountain dependency gate. |
| Outcrops | Pending | Validate coverage/breakage/surface detail; current implementation is a simple slope/curvature ridge boost. |
| Pockmarks | Pending | Validate scatter/dent controls. |
| RockNoise | Contract mismatch / Behavior mismatch | See Mountain dependency gate. |
| Rockscape | Pending | Validate broad rock formation contract. |
| Roughen | Pending | Validate fine breakup controls. |
| Sand | Pending | Validate medium/small sand pattern controls. |
| Sandstone | Pending | Validate sedimentary layering and outputs. |
| Shatter | Pending | Validate fracture transformation. |
| Shear | Pending | Validate rock shearing and exposed-strata behavior. |
| Steps | Pending | Validate hard step controls. |
| Stones | Pending | Validate scatter controls and scale. |
| Stratify | Contract mismatch / Behavior mismatch | See Mountain dependency gate. |
| Terraces | Pending | Validate terrace modes and controls. |

## Simulate

| Node | Status | Notes |
|---|---|---|
| Anastomosis | Pending | Flow-analysis demand is already minimized; morphology still needs Gaea fidelity review. |
| Crumble | Pending | Flow-analysis demand fixed; behavior contract still needs review. |
| Debris | Pending | Validate debris transport/deposition. |
| Dusting | Pending | Cryosphere behavior exists; validate public controls. |
| EasyErosion | Pending | Validate style presets and influence/direction behavior. |
| Erosion | Pending | See Mountain dependency gate. |
| Erosion2 | Pending | See Mountain dependency gate. |
| Glacier | Pending | Validate glacial erosion/deposition and masks. |
| Hillify | Pending | Validate documented hill-generation transformation. |
| HydroFix | Pending | Uses hydrology-network tier; validate correction behavior. |
| IceFloe | Pending | Validate ice breakup controls. |
| Lake | Pending | Basin fill is physically separate from global hydrology; validate Gaea controls/outputs. |
| Lichtenberg | Pending | Correctly needs no hydrology; validate morphology controls. |
| Rivers | Pending | Uses hydrology-network tier; validate width/depth/headwater semantics. |
| Scree | Pending | Validate talus/scree deposition. |
| Sea | Pending | Boundary-connected below-zero ocean model; validate Gaea controls/outputs. |
| Sediments | Pending | Validate current multi-pass physical sediment contract. |
| Shrubs | Pending | Uses flow-analysis only; validate ecology controls. |
| Snow | Pending | Validate accumulation/melt controls. |
| Snowfield | Pending | Validate snowfield-specific behavior. |
| Thermal | Pending | See Mountain dependency gate. |
| Thermal2 | Pending | See Mountain dependency gate. |
| Trees | Pending | Uses flow-analysis only; validate ecology controls. |
| Wizard | Pending | Composite behavior requires full reference review. |
| Wizard2 | Pending | Composite behavior requires full reference review. |

## Derive

| Node | Status | Notes |
|---|---|---|
| Angle | Pending | Validate selector semantics. |
| Curvature | Pending | Validate modes/range. |
| Height | Pending | Validate elevation selector behavior. |
| FlowMap | Pending | Modern version correctly avoids StreamOrder; validate outputs/controls. |
| FlowMapClassic | Pending | Full hydrology/StreamOrder is intentional; validate classic output. |
| Peaks | Pending | Validate peak-selection contract. |
| RockMap | Pending | Validate rock coverage factors. |
| Soil | Pending | Audit still needs to confirm it uses only required flow-analysis tier and matches Gaea soil-map behavior. |
| Normals | Pending | Validate encoding and axes. |
| Occlusion | Pending | Validate sedimentary-process-biased occlusion rather than lighting AO. |
| TextureBase | Pending | Validate chaos/slope/flow/geology factors. |
| Texturizer | Pending | Validate style presets and secondary effects. |
| ColorThreshold | Pending | Validate color threshold behavior. |

## Utility

| Node | Status | Notes |
|---|---|---|
| Accumulator | Unsupported architecture / mismatch | Current implementation combines up to four explicit inputs; not yet audited as Gaea's accumulator semantics. |
| Chokepoint | Pending | Validate graph-control behavior. |
| Combine | Corrected | See Mountain dependency gate. |
| Compare | Pending | Validate ratio/perpendicular/swap outputs. |
| DataExtractor | Behavior mismatch | Current node extracts EONFORM semantic fields; verify current Gaea DataExtractor semantics before claiming parity. |
| Edge | Pending | Current ZeroBorders compatibility node is surfaced as Edge; validate actual Gaea Edge operation. |
| Gate | Unsupported architecture | Current node is a boolean input selector, not a true graph/bake boundary. |
| Layers | Behavior mismatch | Current fixed four-layer opacity stack does not represent current dynamic Gaea Layers behavior fully. |
| LoopBegin | Not implemented | Requires evaluator scope/iteration architecture. |
| MacroPort | Not implemented | Requires macro graph architecture. |
| LoopEnd | Not implemented | Requires evaluator scope/iteration architecture. |
| Mask | Pending | See Mountain dependency gate. |
| Math | Pending | Parser/precedence/function fidelity needs review. |
| Mixer | Pending | Gaea 2 uses Combine for color mixing; verify whether public Mixer should remain. |
| Repeat | Pending | Validate tiling/height compensation. |
| Reseed | Unsupported architecture | Current pass-through cannot mutate upstream seed scope. |
| Route | Behavior mismatch | Current implementation fans one input to four outputs rather than current Gaea routing semantics. |
| Seamless | Pending | Validate scalar/color edge treatment. |
| Switch | Behavior mismatch | Current indexed four-way selector differs from current documented Switch behavior. |
| Var | Unsupported architecture | Current pass-through lacks graph variable state. |

## Immediate remediation order

1. Primitive foundations: RadialGradient, Voronoi, Shape, MultiFractal, Cracks.
2. Utility composition: Combine, Mask, Autolevel/Equalize range processing.
3. Spatial shaping: Warp, SlopeWarp, Shaper, Transpose.
4. Mountain surface detail: RockNoise, GroundTexture, Stratify, Outcrops/Craggy as required.
5. Physical processes: Erosion, Erosion2, Thermal, Thermal2, Sediments/Scree.
6. Only then rebuild Mountain as a visible internal recipe made from those verified nodes.
7. Continue the audit through every remaining Pending node before using it in future Terrain composites.

## Regression tests

Current fidelity tests:

- `CodenameGaea.Core.Graph.ReferenceFidelity.Primitives`
  - RadialGradient honors target resolution and rectangular physical domain.
  - Voronoi internal warp changes output.
  - Voronoi Scale X/Y changes output.
  - Voronoi X/Y transform changes output.
- `CodenameGaea.Core.Graph.ReferenceFidelity.CombineMask`
  - white mask selects Input1.
  - black mask selects Input2.

Existing shallow `CurrentGaeaNodeContracts` remains useful for broad inventory, but it is no longer considered evidence of behavioral fidelity by itself.
