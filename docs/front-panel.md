# Front panel (surface plate)

Mechanical package for the **operator UI** on the SolRx E-Series medium-frame master: a **new surface plate** on the front of the unit (not a reuse of the stock timer cutout).

Holds only the three face parts. MCU, screw-terminal hub, SSR, USB charger, and piezo stay **inside** the chassis.

Related: [lcd1602-i2c.md](lcd1602-i2c.md) · [seven-segment-display.md](seven-segment-display.md) · [keypad-i2c.md](keypad-i2c.md) · [peripherals.md](peripherals.md) · [esp32-board.md](esp32-board.md)

## Status

| Item | State |
|------|--------|
| Mounting strategy | **New surface plate** on the front of the device |
| Layout | Top → bottom: **LCD1602**, **TM1637**, **4×4 keypad** |
| Alignment | All three **centered on the plate vertical centerline** (left–right centered in one column) |
| Part dimensions | **Not yet calipered on this build** — tables below are community / listing nominals + design tolerances |
| CAD / print | OpenSCAD layout: [mechanical/front-panel.scad](mechanical/front-panel.scad) (faces coplanar with keypad) |

## Layout (operator-facing)

```text
        │ centerline
        ▼
┌──────────────────────────────┐  ← surface plate (width ≥ keypad)
│                              │
│      ┌────────────────┐      │
│      │   LCD 16×2     │      │  status / prompts
│      └────────────────┘      │
│                              │
│        ┌──────────┐          │
│        │  TM1637  │          │  clock / countdown
│        └──────────┘          │
│                              │
│      ┌────────────────┐      │
│      │   4×4 KEYPAD   │      │  entry / start / stop
│      │                │      │
│      └────────────────┘      │
│           ribbon ↓           │  keypad tail → I²C adapter (behind)
└──────────────────────────────┘
```

- **Plate width** follows the widest part (almost always the membrane keypad).
- **LCD** and **TM1637** are narrower; keep them on the same centerline, not left-aligned to the keypad edge.
- Gaps between parts are free parameters until measured sizes exist; leave room for bezel lips and finger clearance on the pad.

### What is *not* on the plate

| Part | Location |
|------|----------|
| ESP32 + screw-terminal breakout | Inside chassis (LV hub) |
| Keypad **PCF8574** I²C adapter | Behind panel (ribbon from pad) |
| SSR-25DA, mains load wiring | Inside; isolated from UI harness |
| USB wall charger + internal AC receptacle | Inside |
| Piezo | Inside or edge; not required on face |

## Connectors (fixed constraint)

UI modules keep their existing **0.1″ pin headers**. Female **Dupont** is the interface; do **not** desolder or replace those headers for v1.

| Module | Header on board | Panel / harness side |
|--------|-----------------|----------------------|
| LCD1602 backpack | 4-pin: **GND · VCC · SDA · SCL** | Female Dupont |
| Keypad I²C adapter | 4-pin: **SDA · SCL · VCC · GND** (silk may reorder) | Female Dupont |
| TM1637 | 4-pin: **CLK · DIO · VCC · GND** (or dual edge) | Female Dupont |

There is no project requirement for a special ESP32 “dev board with per-bus 4-pin headers.” The **screw-terminal adapter** is the LV hub. Generic multi-drop boards (Qwiic / STEMMA / Grove) use different plugs and need adapters on every module — optional later, not baseline.

### Two different 4-wire groups

| Bundle | Signals | Topology |
|--------|---------|----------|
| **I²C** | GND, VCC, SDA, SCL | **Shared** — LCD + keypad paralleled |
| **TM1637** | GND, VCC, CLK, DIO | **Point-to-point** only (not on I²C) |

### Preferred harness (v1)

**Home-run** from each module to the ESP32 screw terminals (kill the breadboard):

1. LCD → 4-wire Dupont → screws  
2. Keypad adapter → 4-wire → same SDA/SCL/VCC/GND (second ferrule or doubled wire in the screw)  
3. TM1637 → 4-wire → CLK / DIO / VCC / GND  

Optional later: small passive I²C parallel block (multiple 4-pin headers) if screw terminals get crowded. Strain-relieve where cables leave the plate and enter the chassis.

### Locked electrical map (from bench)

| Function | ESP32 | Notes |
|----------|-------|--------|
| I²C SDA / SCL | **21** / **22** | LCD + keypad |
| LCD address | **0x27** | Confirmed on this unit |
| Keypad address | **0x20** | Confirmed on this unit |
| TM1637 CLK / DIO | **18** / **23** | Not I²C |
| SSR | **26** | Fail-off default LOW; **not** on front plate |
| Piezo | **25** | Not on front plate |

Bench jumper colors (I²C): SDA **orange**, SCL **yellow** — document if the permanent harness reuses them; pin numbers win if colors change.

## Mechanical dimensions and tolerances

### How to read these numbers

| Column | Meaning |
|--------|---------|
| **Nominal** | Common published / community value for this *class* of part — **not** yet measured on the units in this project |
| **Part tolerance (est.)** | How much the physical module may differ from nominal across clones / batches before we caliper *this* board |
| **CAD allowance** | Extra clearance to put in the **plate** design so parts still fit after print/cut error and part variance |

Until the **Measured (this unit)** column is filled, do **not** treat nominals as fabrication truth. Prefer a cardboard mockup after first measure.

**Print / cut process error** (separate from part tolerance): assume roughly **±0.2–0.5 mm** for FDM on hole centers and window edges unless the printer is dialed in tighter. Design allowances below already cover typical FDM + clone variance.

### LCD1602 (+ I²C backpack)

Industry-standard **character LCD** mechanical pattern. Instrument: cheap plastic **analog** calipers where noted.

| Feature | Nominal | Part tolerance (est.) | CAD allowance | Measured (this unit) |
|---------|---------|------------------------|---------------|----------------------|
| Module outer / PCB (L × W) | **80 × 36 mm** | **±0.5 mm** typical; occasional clones **±1 mm** | Pocket / frame: **+0.3 to +0.5 mm** per side if capturing the PCB | *not yet* (bezel only) |
| **Bezel face** (metal/plastic frame around the glass) | often slightly under full PCB | Mold / batch variance small once measured | Front opening if bezel-flush: bezel **+0.5 to +1.0 mm** L and W | **71 mm** wide × **24 mm** high |
| Mounting hole centers | **75 × 31 mm** | **±0.2–0.3 mm** (hole pattern is usually the tightest feature) | Screw holes in plate: **⌀ 2.7–3.0 mm** for M2.5 (clearance fit) | *not yet* |
| Mounting hole ⌀ on module | ~**2.5–2.6 mm** (M2.5 class) | — | Use clearance in the *plate*, not oversize on the module | *not yet* |
| Viewing / glass (inside bezel) | ~**64.5 × 14–16 mm** typical | Window height often **±1–2 mm** between brands | If cutout is glass-only: glass **+0.5 to +1.0 mm**; if cutout shows whole bezel face, use bezel row | *not yet* (optional; bezel is enough for a full-face window) |
| Character area | ~**56 × 11.5 mm** | — | Keep cutout ≥ glass; do not size only to character box | — |
| Module + backpack depth | ~**12 mm** LCD body + backpack ~**8–15 mm** extra | Backpack height varies | Behind plate: plan **≥ 20 mm** free depth for backpack + Dupont | *not yet* |

Suggested first-cut front opening (bezel-flush):

| Cutout | Size |
|--------|------|
| LCD bezel opening | **~72 × 25 mm** (71×24 + ~0.5–1.0 mm clearance) |

**Still needed:** PCB outer if different from bezel stack; hole centers; glass-only size (only if the plate should hide the bezel chrome/frame); backpack depth; header edge.

**Design note:** four-hole pattern is the screw footprint. For a simple plate, opening to the **bezel face (71×24)** is fine; a tighter glass-only window is optional cosmetics.

### TM1637 4-digit clock module

Preferred part: [B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71) (listing **0.56″** digits). Published “**42 × 24 mm**” community numbers are almost always for **0.36″** digit modules — **do not use those for this board.**

Instrument: cheap plastic **analog** calipers (readability limited; treat face height as approximate).

| Feature | Nominal (by class) | Part tolerance (est.) | CAD allowance | Measured (this unit) |
|---------|--------------------|------------------------|---------------|----------------------|
| **LED housing / digit face** (not PCB) | — | Housing mold variance usually small once measured | Front cutout: face **+0.5 to +1.0 mm** L and W so bezel does not clip segments | **50 mm** wide × **~18.4 mm** high (**±0.1 mm** on height; width read as **50 mm**) |
| PCB outer (L × W) | 0.36″ class often 42×24; 0.56″ class often larger | High until measured | Frame/pocket only after measure | *not yet* (housing only) |
| Hole centers | class-dependent | ±0.3 mm once known | Clearance for M2 / #2 | *not yet* |
| Module depth (PCB + housing) | ~12 mm class | — | Behind plate clearance TBD | *not yet* |

**What “LED housing” means for the plate:** the black (or colored) plastic digit block that sits proud of the PCB. That size is the **front window** cutout target. The PCB is wider/taller than 50×18.4 and still needs hole centers and outer outline for standoffs / recess.

Suggested first-cut window (CAD, before PCB measure):

| Cutout | Size |
|--------|------|
| TM1637 digit opening | **~51 × 19.5 mm** (50×18.4 + ~0.5–1.0 mm clearance) |

**Still needed on this module:** PCB outer L×W; mounting hole centers and ⌀; which edge(s) have the 4-pin header; overall depth.

**Design note:** four corner holes (when present) are the screw footprint. Header edge drives cable routing behind the plate.

### 4×4 membrane keypad (+ separate I²C adapter)

**No screw footprint** on the soft pad. “Footprint” = outer outline + adhesive plane + ribbon exit. Measured with a ruler (inches) where noted.

| Feature | Nominal | Part tolerance (est.) | CAD allowance | Measured (this unit) |
|---------|---------|------------------------|---------------|----------------------|
| Membrane outer **height** (tall axis) | often **~69–77 mm** | **±1–3 mm** between kits | Recess **+0.5–1.0 mm** if pocketed | **3″ − 1/32″ ≈ 75.4 mm** (user also: “perhaps **76 mm**”) |
| Membrane outer **width** | often similar to height | **±1–3 mm** | Plate width ≥ pad; recess **+0.5–1.0 mm** | **2 11/16″ = 2.6875″ ≈ 68.3 mm** |
| Active key area | slightly inside outer | — | Do not put bezel lips over keys | *not yet* |
| Thickness | ~**1 mm** class (membrane only) | — | Shallow recess or flat face + optional edge frame against peel | *not yet* |
| Ribbon | 8-pin tail, usually **bottom** edge | Length / exit side can vary | Slot or channel to I²C adapter behind plate | *not yet* |
| I²C adapter board | small PCB (kit-specific) | — | Behind plate; not on the face; leave Dupont clearance | *not yet* |

**Locked outline (ruler):** about **68.3 mm wide × 75.4–76 mm tall** — slightly taller than wide. **Plate outer width** must be **≥ ~68.3 mm** plus margins (LCD bezel is **71 mm** wide, so the **LCD sets the minimum face width** among the three faces unless the plate has side margins for the keypad only).

**Design note:** adhesive mount to a flat plate is the intended install. Ribbon exit edge still TBD.

### Face sizes locked so far (front openings)

Useful for a cardboard / CAD sketch on the **vertical centerline** (top → bottom). Clearances not applied here — raw measured faces:

| Part | Face / bezel (W × H) | Source |
|------|----------------------|--------|
| LCD bezel | **71 × 24 mm** | Analog calipers |
| TM1637 LED housing | **50 × ~18.4 mm** | Analog calipers |
| Keypad membrane | **≈ 68.3 × 75.4–76 mm** (W × H) | Ruler: **2 11/16″** × (**3″ − 1/32″**) |

Widest face: **LCD bezel (71 mm)**. Tallest face: **keypad (~76 mm)**.

With ~0.5–1.0 mm cutout clearance (CAD):

| Opening | Suggested first cut |
|---------|---------------------|
| LCD | **~72 × 25 mm** |
| TM1637 | **~51 × 19.5 mm** |
| Keypad recess | **~69 × 76.5–77 mm** (W × H) |

### Summary: measurement confidence

| Part | Confidence | Notes |
|------|------------|--------|
| **LCD bezel** | **Good** | **71 × 24 mm**; PCB / holes still open |
| **LCD holes / PCB** | **Medium** (community) | Nominal 80×36 / 75×31 until measured |
| **TM1637 face** | **Good** | **50 × ~18.4 mm** housing |
| **TM1637 PCB** | **Low** | Not measured; not the 0.36″ 42×24 footprint |
| **Keypad outline** | **Good enough** | **≈ 68.3 × 75.4–76 mm** (2 11/16″ × ~3″) |

### Measure-later checklist (calipers)

Record to **0.1 mm** if possible (0.5 mm is better than nothing). Cheap analog calipers: prefer repeating a read and noting ± guess.

1. LCD: ~~bezel L×W~~ **done (71 × 24 mm)**; still: PCB outer; hole centers L×W; optional glass L×W; depth with backpack; which edge has the 4-pin header  
2. TM1637: ~~digit face L×W~~ **done (50 × ~18.4 mm housing)**; still: PCB outer L×W; hole centers L×W; hole ⌀; overall depth; header edge(s)  
3. Keypad: ~~outer L×W~~ **done (≈ 68.3 × 75.4–76 mm)**; still: thickness; ribbon exit edge and free length  
4. Keypad adapter: outer L×W; header positions  
5. SolRx front: flat area available for the plate; preferred attach method (screws / VHB / other); depth behind front skin  

Paste values into the **Measured** columns above when done.

## Paper dry-fit and cardboard prototype (print)

| File | Contents |
|------|----------|
| [mechanical/front-panel-cardboard.pdf](mechanical/front-panel-cardboard.pdf) | **1:1 cut template** (same layout as OpenSCAD). **Page 1:** plate outline + openings + ribbon slot. **Page 2:** face-only outlines for dry-fit. |
| [mechanical/letter-graph-paper-mm.pdf](mechanical/letter-graph-paper-mm.pdf) | **Page 1:** blank 1 mm / 5 mm / 10 mm grid. **Page 2:** true-size dashed faces on grid. |

Regenerate after measurement / layout updates:

```bash
python3 scripts/gen-front-panel-cardboard.py
python3 scripts/gen-letter-graph-paper.py
```

**Print settings:** 100% / **Actual size** — do **not** use “fit to page.” After printing, check that the **100 mm** scale bar is exactly 100 mm on a ruler before cutting.

**Cardboard steps (page 1):** cut outer plate → cut solid openings (LCD / TM / keypad; include printed clearance) → optional mark dashed true-face size → ribbon slot at bottom → tape modules from behind so faces sit flush with the front.

## OpenSCAD layout

Parametric plate + face openings (measured face sizes; **display faces coplanar with the keypad** front plane):

| File | Role |
|------|------|
| [mechanical/front-panel.scad](mechanical/front-panel.scad) | Source — Customizer params for sizes, gaps, margins |
| [mechanical/front-panel-preview.png](mechanical/front-panel-preview.png) | Ortho front preview |
| [mechanical/front-panel-preview-iso.png](mechanical/front-panel-preview-iso.png) | Isometric preview |

```bash
openscad docs/mechanical/front-panel.scad
# optional PNG
openscad -o /tmp/front-panel.png --imgsize=1400,1000 \
  --camera=0,0,200,0,0,0,0 --projection=ortho docs/mechanical/front-panel.scad
```

Defaults: **10 mm** gaps, plate ~**95 × 160 × 3 mm**, ribbon slot under keypad. Toggle `show_back_bulk` for placeholder backpack depths (not calipered).

## Fabrication notes (when CAD starts)

- **Paper dry-fit** first (PDF above), then cardboard mockup on the real unit; check reach, glare, and centerline alignment.  
- **Front openings** expose glass / digits / keys only; hide PCB edges and pin headers.  
- **Two-piece sandwich** (front bezel + back plate / standoffs) eases service without reprinting the whole plate.  
- **Strain relief** at panel cable exit.  
- Keep **mains / SSR** wiring completely separate from the UI Dupont harness.

## Open decisions

- [ ] Caliper the three UI modules (table above)  
- [ ] SolRx face attach method and plate outer size  
- [ ] 3D print vs other plate material  
- [ ] Single multi-pin panel connector vs home-run Dupont only  
- [x] First OpenSCAD layout: `docs/mechanical/front-panel.scad` (faces flush; depths TBD) 

## Revision

| Date | Note |
|------|------|
| 2026-07-19 | Initial: surface plate, vertical stack, centerline, Dupont constraints, nominal dims + tolerances |
| 2026-07-19 | TM1637 LED housing measured: **50 × ~18.4 mm** (not including PCB); analog calipers |
| 2026-07-19 | LCD bezel **71 × 24 mm**; keypad **2 11/16″ × (~3″ − 1/32″)** ≈ **68.3 × 75.4–76 mm** |
| 2026-07-19 | Letter mm graph paper PDF: [mechanical/letter-graph-paper-mm.pdf](mechanical/letter-graph-paper-mm.pdf); generator `scripts/gen-letter-graph-paper.py` |
| 2026-07-19 | OpenSCAD front panel: [mechanical/front-panel.scad](mechanical/front-panel.scad) — LCD/TM/keypad faces coplanar |
| 2026-07-19 | Cardboard 1:1 PDF: [mechanical/front-panel-cardboard.pdf](mechanical/front-panel-cardboard.pdf); `scripts/gen-front-panel-cardboard.py` |
