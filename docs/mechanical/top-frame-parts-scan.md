# Top-frame / enclosure parts scans (measurement reference)

Two Brother MFC-J1010DW flatbed captures of the stock white metal housing that sits at the top of the phototherapy fixture (control / “brain” bay). Same scale and capture settings for both.

| Field | Value |
|-------|--------|
| Device | Brother MFC-J1010DW (network, eSCL) |
| Source | Flatbed |
| Mode | Color |
| Resolution | **600 DPI** |
| Image size | **6978 × 5070 px** (after 90° CW rotate; bed was scanned portrait) |
| Physical bed area | 295.4 × 214.6 mm (11.63 × 8.45 in) as stored |
| Scale | **1 mm ≈ 23.622 px** · **1 in = 600 px** · 1 px ≈ 42.33 µm |
| Orientation | Masters + previews rotated **90° clockwise** so the long metal runs more naturally left–right |

## Scan 1 — outer top face (+ displays, first layout)

Larger metal piece scanned as the **top** of the assembly (outer / top face), with LCD1602, TM1637, and ruler for scale. Kept even though the first pass also included other items.

| Item | Notes |
|------|--------|
| Large white metal panel | Long strip with control cutouts / holes / slots — **top face** of the stock top assembly |
| Second white metal frame | Display-window frame (also on bed in this pass) |
| TM1637 4-digit | Shows `8:88` |
| LCD1602 I²C module | Dark glass module |
| Wooden ruler | cm + inch along left edge |

| File | Role |
|------|------|
| [top-frame-top-face-scan-600dpi.png](top-frame-top-face-scan-600dpi.png) | Master |
| [top-frame-top-face-scan-preview.jpg](top-frame-top-face-scan-preview.jpg) | Color preview |
| [top-frame-top-face-scan-preview-contrast.jpg](top-frame-top-face-scan-preview-contrast.jpg) | High-contrast edges |

## Scan 2 — front of top + enclosure mate

Ruler, **front of the top** (operator-facing strip), and the **second piece** that completes the enclosure for the control electronics bay.

| Item | Notes |
|------|--------|
| Front-of-top strip | Long bar with a **rectangular cutout for the TM1637 LED** and **two bolt holes** on the mating edge |
| Enclosure mate | White frame under the front strip: **LCD** + **keypad** openings; **three holes** on mating edge (outer two match front; middle mate-only) |
| Wooden ruler | cm + inch along left edge |
| Small wood scrap | Mid-bed prop/spacer only; ignore for dimensions |

| File | Role |
|------|------|
| [top-frame-front-and-enclosure-scan-600dpi.png](top-frame-front-and-enclosure-scan-600dpi.png) | Master |
| [top-frame-front-and-enclosure-scan-preview.jpg](top-frame-front-and-enclosure-scan-preview.jpg) | Color preview |
| [top-frame-front-and-enclosure-scan-preview-contrast.jpg](top-frame-front-and-enclosure-scan-preview-contrast.jpg) | High-contrast edges |

**Isolated parts** (ruler + wood block removed; black background; 600 DPI):

| File | Part |
|------|------|
| [top-frame-front-strip-isolated-600dpi.png](top-frame-front-strip-isolated-600dpi.png) | Front-of-top strip |
| [top-frame-enclosure-mate-isolated-600dpi.png](top-frame-enclosure-mate-isolated-600dpi.png) | Enclosure mate |
| [top-frame-front-parts-isolated-preview.jpg](top-frame-front-parts-isolated-preview.jpg) | Side-by-side preview |

### How the two metal parts fit (assembly, not scan orientation)

In the fixture, the **enclosure mate bolts on behind the front** (operator sees the front strip; electronics bay is closed by the second piece from the rear).

| Feature | Front strip | Enclosure mate (second piece) |
|---------|-------------|-------------------------------|
| Bolt holes | **Two** | **Three** on the mating edge |
| Match | Outer pair of the three | Align with the front’s two |
| Middle hole | — | Extra hole on the mate only (not used by the front’s two-hole pattern) |

**Scan orientation caveat:** on the bed, the second piece is rotated about **90°** relative to how it actually mates to the front. In the scan the three-hole edge runs roughly horizontal; when assembled that edge stands vertical and lines up with the front’s two-hole edge, with the mate sitting **behind** the front.

```text
  Operator side                         Behind / electronics bay
  ─────────────                         ────────────────────────

  ┌ front strip ─┐                      ┌ enclosure mate ──────┐
  │   [window]  ○│  ← bolt (outer)  →   │○  … openings …       │
  │             │                       │○  (middle, mate only)│
  │             ○│  ← bolt (outer)  →   │○                     │
  └──────────────┘                      └──────────────────────┘
         ▲                                        ▲
         │         mate bolts ON BEHIND front     │
         └──────── shared outer-hole spacing ─────┘
```

When transferring hole centers from scan 2 into CAD, measure each part in its **as-scanned** pose, then rotate the mate’s hole line by ~90° (and place it behind the front) before combining into one assembly model.

## Isolated vs assembled

Quantitative compare of the isolated parts against the bolted assembly: [top-frame-isolated-vs-assembled.md](top-frame-isolated-vs-assembled.md). Outer bolt pitch agrees to ~0.2 mm.

## Scan 3 — parts assembled

Front strip and enclosure mate **bolted together** as they mate in the fixture (operator-facing strip on the left in the scan; mate body to the right). No ruler on this pass — use prior scans for scale (same 600 DPI bed).

| File | Role |
|------|------|
| [top-frame-assembled-scan-600dpi.png](top-frame-assembled-scan-600dpi.png) | Master |
| [top-frame-assembled-scan-preview.jpg](top-frame-assembled-scan-preview.jpg) | Color preview |
| [top-frame-assembled-scan-preview-contrast.jpg](top-frame-assembled-scan-preview-contrast.jpg) | High-contrast edges |
| Tool | `./scripts/scan-bed scan top-frame-assembled` |

## 2D assembly drawing

Scan-derived **1:1** overlay with **one PDF/OCG layer per part** (mate centered; front-of-top strip runs off the page left and right; outer bolt holes aligned):

| File | Role |
|------|------|
| [top-frame-assembly-2d.pdf](top-frame-assembly-2d.pdf) | Printable assembly (toggle layers in a PDF viewer that supports OCG) |
| [top-frame-assembly-2d.scad](top-frame-assembly-2d.scad) | Editable 2D CAD (OpenSCAD) — dims from isolated masters |
| [top-frame-assembly-2d-preview.png](top-frame-assembly-2d-preview.png) | Quick preview |
| Generator | `python3 scripts/gen-top-frame-assembly-pdf.py` (keep in sync with `.scad`) |

Hole spacing used: **63.38 mm** (isolated front 63.39 / mate outer 63.37 / assembled 63.17). See [top-frame-isolated-vs-assembled.md](top-frame-isolated-vs-assembled.md).

## Using for measurements

1. Open a master PNG in any tool that respects DPI (or use the scale table above).
2. Prefer measuring **against the in-frame ruler** for local calibration.
3. Metal panels flat on glass give the sharpest edges; thick modules or wood props cast soft shadow and should not be used as length standards.
4. Account for the **~90° scan rotation** of the enclosure mate when combining front + mate dimensions (see assembly section above).
5. Shop reference for CAD / front-panel fit only — not production metrology or medical dosing.

Related: [front-panel.md](../front-panel.md), [front-panel.scad](front-panel.scad).
