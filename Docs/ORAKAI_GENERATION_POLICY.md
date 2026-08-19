# Orakai Island Generation Policy

Orakai uses Codename Gaea as its terrain-generation engine, but players do not receive the unrestricted commercial authoring surface.

The Orakai island creator will submit a runtime-safe terrain recipe through a strict generation policy before evaluation.

The policy is responsible for constraining at least:

- maximum island world extent
- allowed grid resolutions and runtime quality tiers
- permitted node/operation types
- per-node parameter ranges
- erosion and simulation iteration limits
- graph depth / node-count limits
- memory and estimated-work budgets
- guard-band/tile limits
- allowed runtime output backends
- any gameplay-specific requirements such as mandatory coastline, spawnable land area, or navigation viability

The policy belongs above the generic Codename Gaea evaluator. Codename Gaea remains capable of unrestricted professional authoring for commercial plugin customers, while Orakai supplies a curated policy/profile and a simpler player-facing UI.

Conceptually:

```text
Orakai Island Creator
        |
        v
Player Recipe
        |
        v
Orakai Generation Policy
  validate / clamp / reject
        |
        v
Codename Gaea Runtime Evaluator
        |
        v
FGaeaTerrainDataset
        |
        +--> Runtime geometry
        +--> Gameplay terrain fields
```

This avoids relying on UI controls alone for safety. Invalid or excessive recipes must be rejected or normalized by policy even if they come from saves, networking, mods, or future alternate front ends.
