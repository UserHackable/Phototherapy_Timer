# Isolated parts vs assembled scan

All **600 DPI** (1 mm ≈ 23.622 px). Hole centers refined by dark-centroid on metal.

## Outer bolt spacing

| Source | Outer hole spacing |
|--------|-------------------:|
| Front strip (isolated) | **63.39 mm** |
| Enclosure mate (isolated, outer of 3) | **63.37 mm** |
| Assembled scan (two visible bolts) | **63.17 mm** |

| Delta | mm |
|-------|---:|
| front − mate | +0.02 |
| front − assembled | +0.22 |
| mate − assembled | +0.20 |

Max pairwise |delta| ≈ **0.22 mm** (consistent with flatbed + isolation noise).

## Metal bbox (approx, isolation padding included)

| Part | Width × height |
|------|----------------|
| Front isolated | 295.4 × 54.0 mm |
| Mate isolated | 91.0 × 157.5 mm |
| Assembled (combined silhouette) | 295.4 × 214.6 mm |

## Pose / topology

| Check | Result |
|-------|--------|
| Mate after 90° CW | Three-hole edge on **top**; body hangs **down** — same as assembled |
| Shared bolts | Assembled shows **two** = front pair = mate **outer** pair |
| Mate middle hole | Mate only; no front partner |
| Overlay | Mate behind, front on top, outer holes coincident |

## Figures

- [top-frame-isolated-vs-assembled-compare.jpg](top-frame-isolated-vs-assembled-compare.jpg)
- [top-frame-isolated-parts-synthetic-assembly-preview.jpg](top-frame-isolated-parts-synthetic-assembly-preview.jpg)

## Conclusion

The isolated front strip and enclosure mate **match the assembled scan**: same outer-hole pitch (~63.3 mm), same relative pose (mate under front), and the expected 2-of-3 hole engagement. Safe to use the isolated masters as the dimensional source for the bolted assembly.
