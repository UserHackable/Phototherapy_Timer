# Component modules scan (measurement reference)

Flatbed capture of the three operator-UI modules for the **stock top frame**.

## Order (top → bottom, as used in the fixture)

| # | Module | Count | Role | Mount in stock top frame |
|---|--------|------:|------|---------------------------|
| 1 | **TM1637 LED** (4-digit) | **1** | Clock / countdown | **Rectangular hole in the front strip** |
| 2 | **LCD1602** I²C | **1** | Status / prompts | **Cover plate** window (upper / wider remainder of mate main hole) |
| 3 | **4×4 membrane keypad** | **1** | Entry / start / stop | On **cover plate** (solid backer; stock bottom slot covered) |
| — | Wooden ruler | — | Scale | — |

**Cover plate** (new part on the enclosure mate): blanks the mate bottom hole, shrinks the large main hole so the LCD16×2 fits the wider portion left, and backs the keypad. See [top-frame-assembly-2d.scad](top-frame-assembly-2d.scad).

## Capture

| Field | Value |
|-------|--------|
| Tool | [`scripts/scan-bed`](../../scripts/scan-bed) |
| Command | `./scripts/scan-bed scan component-modules` |
| Device | Brother MFC-J1010DW (eSCL flatbed) |
| Resolution | **600 DPI** |
| Master | [component-modules-scan-600dpi.png](component-modules-scan-600dpi.png) |
| Previews | [preview](component-modules-scan-preview.jpg) · [contrast](component-modules-scan-preview-contrast.jpg) |
| Scale | **1 mm ≈ 23.622 px** · **1 in = 600 px** |

Modules sit off the glass slightly (housings/PCB), so edges can be soft. Prefer the ruler for length; use calipers for thickness / stack height.

**Note:** Re-scan with all three modules on the bed (LED top, LCD middle, keypad bottom) when a full reference frame is needed. A pass may only show the keypad if the displays were not on the platen.

Related: [front-panel.md](../front-panel.md), [top-frame-parts-scan.md](top-frame-parts-scan.md), [seven-segment-display.md](../seven-segment-display.md).
