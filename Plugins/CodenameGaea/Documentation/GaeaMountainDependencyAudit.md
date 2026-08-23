# EONFORM Mountain Dependency Fidelity Gate

This document is the current implementation gate for rebuilding the public `Mountain` Terrain node as an internal graph. It complements the full inventory in `GaeaNodeFidelityAudit.md`.

A node is allowed into the rebuilt Mountain recipe only when its public Gaea-facing contract has been reviewed and its important controls are implemented rather than decorative.

## Cleared / corrected dependencies

| Node | Status | Current result |
|---|---|---|
| RadialGradient | Corrected | Uses graph target resolution, rectangular physical world dimensions, and Scale/Height/X/Y. |
| Voronoi | Corrected | All advertised transform, non-uniform scale and internal warp controls are consumed; C/N/R/P/A/S/M/D forms are distinct. |
| Combine | Corrected | Gaea 2 mask semantics fixed: white selects Input1, black selects Input2. Range enhancement controls are implemented. |
| Cracks | Corrected | Style/Octaves/Scale/Depth/Jitter/Warp/Seed/ScaleX/ScaleY are active; source now respects graph resolution and physical rectangular domain. |
| Shaper | Verified | Shape, Local Effect/Area and fine-detail preservation perform the documented pre-erosion bulking role. |
| Warp | Corrected | Audited vector-field implementation exposes and consumes warp source, Z scale, perturbation, complexity, roughness, normalization, edge handling, modulation and iterative modes. |
| SlopeWarp | Corrected | Intensity/Iterations/Direction/Normalized plus Low/Medium/High/Ultra Quality and Off/X4/X16 Antialiasing are implemented. |
| RockNoise | Corrected | Standalone rock-field source using Size/Variety/Octaves/Seed/Style A-B-C for Embed/Insert workflows. |
| GroundTexture | Corrected | Method Harsh/Rocky/Rough, Strength, Coverage and Density are implemented. |
| Stratify | Corrected | Spacing, Octaves, Intensity, Shape, Seed, Tilt Amount and Direction are active. |
| Transpose | Corrected | Transpose/Embed/Insert composition is implemented so generated rock fields can be embedded into existing terrain. |
| Outcrops | Corrected | Variations, Strata, Density, Shape, Chipped, Seed, Size, Height and Rotation are all consumed by the evaluator. |
| Craggy | Corrected | Current Size/Depth/Shape/Seed contract drives broken multi-scale rocky surface morphology. |
| Erosion | Corrected | Public Feature Scale is physical metres rather than the old dimensionless 0.25-8 approximation; advanced drainage-network solver remains demand-driven. |
| Erosion2 | Corrected | Erosion Scale supports physical-scale ravines/gullies instead of the old 0.1-8 clamp; Wear/Deposits/Flow are preserved. |
| Thermal | Corrected | Duration/Strength/Anisotropy/Seed/Angle/Settling/Sediment Removal and physical Feature Scale are implemented; Real Scale/Terrain Scale/Verticality are honored. |
| Thermal2 | Corrected | Duration/Strength/Anisotropy/Angle/Sediment Removal and Feature Scale in metres drive a true multi-scale thermal solver. |
| Sediments | Corrected | Public contract is Type/Passes/Scale/Angle/Style/Grainy Deposits; the undocumented public Seed control was removed from the audited descriptor. |
| Scree | Corrected | Stones/Scale/Height/Density/Spread/Edge/Seed are all consumed; the old implementation ignored Stones, Spread and Edge. |

## Supported subset / architecture gap

### Mask

The explicit post-effect operation is correct:

```text
black mask -> Before
white mask -> After
```

This is sufficient for an internal Mountain recipe because the composite can explicitly wire the pre-effect terrain into `Before`.

Gaea 2 can also infer the `Before` terrain automatically from graph history for a simple `Mountain -> Erosion -> Mask` chain. EONFORM does not yet preserve the graph provenance required to infer that upstream state automatically. Automatic-before remains an evaluator architecture task; it must not be faked.

## Reference sources reviewed

Current QuadSpinner Gaea 2 reference pages were used for the contracts above, including:

- `/reference/nodes/primitive/cracks.html`
- `/reference/nodes/modify/shaper.html`
- `/reference/nodes/modify/slopewarp.html`
- `/reference/nodes/utility/mask.html`
- `/reference/nodes/surface/outcrops.html`
- `/reference/nodes/surface/craggy.html`
- `/reference/nodes/simulate/erosion.html`
- `/reference/nodes/simulate/erosion2.html`
- `/reference/nodes/simulate/thermal.html`
- `/reference/nodes/simulate/thermal2.html`
- `/reference/nodes/simulate/sediments.html`
- `/reference/nodes/simulate/scree.html`

## Regression gate

The current behavior tests include:

- `CodenameGaea.Core.Graph.ReferenceFidelity.Primitives`
- `CodenameGaea.Core.Graph.ReferenceFidelity.CombineMask`
- `CodenameGaea.Core.Graph.ReferenceFidelity.MountainSurfaceComposition`
- `CodenameGaea.Core.Graph.ReferenceFidelity.ErosionScale`
- `CodenameGaea.Core.Graph.ReferenceFidelity.ThermalScale`
- `CodenameGaea.Core.Graph.ReferenceFidelity.SlopeWarp`
- `CodenameGaea.Core.Graph.ReferenceFidelity.RockSurface`
- `CodenameGaea.Core.Graph.ReferenceFidelity.SedimentScree`
- `CodenameGaea.Core.Graph.ReferenceFidelity.CracksResolution`
- `CodenameGaea.Core.Hydraulic.AdvancedChannelFormation`

## Mountain rebuild rule

The previous bespoke experimental Mountain implementation is not accepted as the final Terrain node.

The replacement must be an authored internal recipe composed from the audited nodes above. No hidden `preserve clean base` blend, no custom ridge formula standing in for nodes, and no repainting the pre-erosion source over the processed result.
