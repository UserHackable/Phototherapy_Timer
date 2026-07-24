#!/usr/bin/env python3
"""Generate 1:1 2D assembly PDF of top-frame + cover plate + modules.

Output: docs/mechanical/top-frame-assembly-2d.pdf

Stack (top → bottom), one of each module:
  - TM1637 LED in front-strip rectangular hole
  - LCD1602 in cover-plate window (upper wider remainder of mate main hole)
  - 4×4 keypad on cover plate (bottom mate slot covered; keypad backer)

Cover plate (new part on mate face):
  - Blanks mate bottom slot
  - Shrinks mate main opening to an LCD-sized cutout
  - Solid face for keypad backing

Keep in sync with docs/mechanical/top-frame-assembly-2d.scad.
"""
from __future__ import annotations

import os
import sys

PAGE_W, PAGE_H = 612.0, 792.0
MM = 72.0 / 25.4

# ---- same numbers as top-frame-assembly-2d.scad ----
FRONT_DEPTH = 35.3
FRONT_HOLE_SPACING = 63.38
FRONT_HOLE_D = 5.5
FRONT_HOLE_FROM_MATING = 6.3
LED_WIN_ALONG = 52.0
LED_WIN_ACROSS = 20.2
LED_WIN_FROM_MATING = 10.5
FRONT_DRAW_LEN = 280.0

MATE_W = 88.3
MATE_BODY_H = 124.5
MATE_FEET_EXTRA = 8.0
MATE_HOLE_SPACING = 63.38
MATE_HOLE_D = 5.5
MATE_MID_HOLE_D = 5.3
MATE_MAIN_W = 72.0
MATE_MAIN_H = 75.0
MATE_MAIN_FROM_HOLES = 6.0
MATE_SLOT_W = 50.0
MATE_SLOT_H = 22.0
MATE_SLOT_FROM_FAR = 4.0
FOOT_W = 18.0

TM_W, TM_H = 50.0, 18.4
LCD_W, LCD_H = 71.0, 24.0
KEY_W, KEY_H = 68.3, 75.5
COVER_LCD_CLEAR = 0.8
COVER_LCD_FROM_MAIN_TOP = 1.5
GAP_LCD_KEY = 3.0

ORIGIN_X_MM = (PAGE_W / MM) / 2.0
ORIGIN_Y_MM = (PAGE_H / MM) / 2.0 + 20.0


def pdf_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def mm_to_pt(x_mm: float, y_mm: float) -> tuple[float, float]:
    return ((ORIGIN_X_MM + x_mm) * MM, (ORIGIN_Y_MM + y_mm) * MM)


class Path:
    def __init__(self) -> None:
        self.ops: list[str] = []

    def m(self, x: float, y: float) -> None:
        px, py = mm_to_pt(x, y)
        self.ops.append(f"{px:.2f} {py:.2f} m")

    def l(self, x: float, y: float) -> None:
        px, py = mm_to_pt(x, y)
        self.ops.append(f"{px:.2f} {py:.2f} l")

    def rect(self, x: float, y: float, w: float, h: float) -> None:
        self.m(x, y)
        self.l(x + w, y)
        self.l(x + w, y + h)
        self.l(x, y + h)
        self.ops.append("h")

    def circle(self, cx: float, cy: float, r: float) -> None:
        k = 0.5522847498 * r
        px, py = mm_to_pt(cx, cy)
        self.ops.append(f"{px + r * MM:.2f} {py:.2f} m")
        for c1x, c1y, c2x, c2y, ex, ey in (
            (r, k, k, r, 0, r),
            (-k, r, -r, k, -r, 0),
            (-r, -k, -k, -r, 0, -r),
            (k, -r, r, -k, r, 0),
        ):
            self.ops.append(
                f"{px + c1x * MM:.2f} {py + c1y * MM:.2f} "
                f"{px + c2x * MM:.2f} {py + c2y * MM:.2f} "
                f"{px + ex * MM:.2f} {py + ey * MM:.2f} c"
            )
        self.ops.append("h")


def y_mating() -> float:
    return -FRONT_HOLE_FROM_MATING


def y_free() -> float:
    return FRONT_DEPTH - FRONT_HOLE_FROM_MATING


def y_led0() -> float:
    return y_mating() + LED_WIN_FROM_MATING


def y_main1() -> float:
    return -MATE_MAIN_FROM_HOLES


def y_main0() -> float:
    return y_main1() - MATE_MAIN_H


def cover_lcd_w() -> float:
    return LCD_W + 2 * COVER_LCD_CLEAR


def cover_lcd_h() -> float:
    return LCD_H + 2 * COVER_LCD_CLEAR


def y_cover_lcd1() -> float:
    return y_main1() - COVER_LCD_FROM_MAIN_TOP


def y_cover_lcd0() -> float:
    return y_cover_lcd1() - cover_lcd_h()


def layer_stream_front() -> str:
    lines: list[str] = []
    a = lines.append
    half = FRONT_DRAW_LEN / 2
    a("1.0 0.45 0.15 RG")
    a("1.0 0.85 0.75 rg")
    a("1.2 w")
    p = Path()
    p.rect(-half, y_mating(), FRONT_DRAW_LEN, y_free() - y_mating())
    a("\n".join(p.ops))
    a("B")
    a("1 1 1 rg")
    a("1.0 0.45 0.15 RG")
    a("0.9 w")
    for sx in (-1.0, 1.0):
        p = Path()
        p.circle(sx * FRONT_HOLE_SPACING / 2, 0.0, FRONT_HOLE_D / 2)
        a("\n".join(p.ops))
        a("B")
    p = Path()
    p.rect(-LED_WIN_ALONG / 2, y_led0(), LED_WIN_ALONG, LED_WIN_ACROSS)
    a("\n".join(p.ops))
    a("B")
    return "\n".join(lines)


def layer_stream_mate() -> str:
    lines: list[str] = []
    a = lines.append
    left = -MATE_W / 2
    a("0.15 0.40 0.85 RG")
    a("0.75 0.85 1.0 rg")
    a("1.2 w")
    p = Path()
    p.m(left, 0)
    p.l(left, MATE_FEET_EXTRA)
    p.l(left + FOOT_W, MATE_FEET_EXTRA)
    p.l(left + FOOT_W, 0)
    p.l(left + MATE_W - FOOT_W, 0)
    p.l(left + MATE_W - FOOT_W, MATE_FEET_EXTRA)
    p.l(left + MATE_W, MATE_FEET_EXTRA)
    p.l(left + MATE_W, 0)
    p.l(left + MATE_W, -MATE_BODY_H)
    p.l(left, -MATE_BODY_H)
    p.ops.append("h")
    a("\n".join(p.ops))
    a("B")
    a("1 1 1 rg")
    a("0.15 0.40 0.85 RG")
    a("0.9 w")
    for i, x in enumerate((-MATE_HOLE_SPACING / 2, 0.0, MATE_HOLE_SPACING / 2)):
        d = MATE_MID_HOLE_D if i == 1 else MATE_HOLE_D
        p = Path()
        p.circle(x, 0.0, d / 2)
        a("\n".join(p.ops))
        a("B")
    p = Path()
    p.rect(-MATE_MAIN_W / 2, y_main0(), MATE_MAIN_W, MATE_MAIN_H)
    a("\n".join(p.ops))
    a("B")
    p = Path()
    p.rect(
        -MATE_SLOT_W / 2,
        -MATE_BODY_H + MATE_SLOT_FROM_FAR,
        MATE_SLOT_W,
        MATE_SLOT_H,
    )
    a("\n".join(p.ops))
    a("B")
    return "\n".join(lines)


def layer_stream_cover() -> str:
    """Cover plate: solid mate face with LCD aperture only."""
    lines: list[str] = []
    a = lines.append
    left = -MATE_W / 2
    a("0.15 0.55 0.25 RG")
    a("0.55 0.85 0.60 rg")
    a("1.1 w")
    p = Path()
    p.rect(left, -MATE_BODY_H, MATE_W, MATE_BODY_H)
    a("\n".join(p.ops))
    a("B")
    # LCD window (white knockout)
    a("1 1 1 rg")
    a("0.15 0.55 0.25 RG")
    a("0.9 w")
    p = Path()
    p.rect(-cover_lcd_w() / 2, y_cover_lcd0(), cover_lcd_w(), cover_lcd_h())
    a("\n".join(p.ops))
    a("B")
    # bolt clearances
    for i, x in enumerate((-MATE_HOLE_SPACING / 2, 0.0, MATE_HOLE_SPACING / 2)):
        d = (MATE_MID_HOLE_D if i == 1 else MATE_HOLE_D) + 0.5
        p = Path()
        p.circle(x, 0.0, d / 2)
        a("\n".join(p.ops))
        a("B")
    return "\n".join(lines)


def layer_stream_modules() -> str:
    lines: list[str] = []
    a = lines.append
    y_led_c = y_led0() + LED_WIN_ACROSS / 2
    y_lcd_c = (y_cover_lcd0() + y_cover_lcd1()) / 2
    y_key1 = y_cover_lcd0() - GAP_LCD_KEY
    y_key0 = y_key1 - KEY_H

    # single TM1637
    a("0.15 0.45 0.90 RG")
    a("0.55 0.75 0.98 rg")
    a("0.9 w")
    p = Path()
    p.rect(-TM_W / 2, y_led_c - TM_H / 2, TM_W, TM_H)
    a("\n".join(p.ops))
    a("B")
    # LCD in cover window
    a("0.1 0.1 0.35 RG")
    a("0.25 0.25 0.45 rg")
    p = Path()
    p.rect(-LCD_W / 2, y_lcd_c - LCD_H / 2, LCD_W, LCD_H)
    a("\n".join(p.ops))
    a("B")
    # keypad on cover backer
    a("0.05 0.05 0.05 RG")
    a("0.2 0.2 0.2 rg")
    p = Path()
    p.rect(-KEY_W / 2, y_key0, KEY_W, KEY_H)
    a("\n".join(p.ops))
    a("B")
    return "\n".join(lines)


def layer_stream_bolts() -> str:
    lines: list[str] = []
    a = lines.append
    a("0.05 0.05 0.05 RG")
    a("0.2 0.2 0.2 rg")
    a("0.8 w")
    for x in (-FRONT_HOLE_SPACING / 2, FRONT_HOLE_SPACING / 2):
        p = Path()
        p.circle(x, 0.0, FRONT_HOLE_D * 0.22)
        a("\n".join(p.ops))
        a("B")
    p = Path()
    p.circle(0.0, 0.0, MATE_MID_HOLE_D * 0.22)
    a("\n".join(p.ops))
    a("B")
    a("0.4 0.4 0.4 RG")
    a("0.5 w")
    a("[3 2] 0 d")
    x0, y0 = mm_to_pt(-120, 0)
    x1, y1 = mm_to_pt(120, 0)
    a(f"{x0:.2f} {y0:.2f} m {x1:.2f} {y1:.2f} l S")
    a("[] 0 d")
    return "\n".join(lines)


def layer_stream_annotations() -> str:
    lines: list[str] = []
    a = lines.append

    def text(x_pt: float, y_pt: float, size: float, s: str, bold: bool = False) -> None:
        font = "/F2" if bold else "/F1"
        a("BT")
        a(f"{font} {size} Tf")
        a(f"1 0 0 1 {x_pt:.2f} {y_pt:.2f} Tm")
        a(f"({pdf_escape(s)}) Tj")
        a("ET")

    text(36, PAGE_H - 36, 12, "Phototherapy Timer — top-frame assembly (2D)", bold=True)
    text(
        36,
        PAGE_H - 52,
        8,
        "1× TM1637 in front LED hole | cover plate: LCD window + keypad backer | bottom slot covered | 1:1 mm",
    )
    text(
        36,
        PAGE_H - 64,
        8,
        "Cover shrinks mate main hole to LCD 16×2; blanks lower slot. PRINT AT 100%.",
    )

    ly = 48
    text(36, ly + 60, 9, "Layers (toggle OCG):", bold=True)
    text(36, ly + 46, 8, "front_strip — LED rectangular hole; 2 bolts")
    text(36, ly + 34, 8, "enclosure_mate — stock metal (openings as scanned)")
    text(36, ly + 22, 8, "cover_plate — blanks bottom slot; LCD cutout; keypad solid back")
    text(36, ly + 10, 8, "modules — 1×TM1637, 1×LCD, 1×keypad  |  bolts")

    a("0 0 0 RG")
    a("1.0 w")
    x0, y0 = mm_to_pt(-MATE_W / 2, -MATE_BODY_H - 12)
    x1, y1 = mm_to_pt(-MATE_W / 2 + 50, -MATE_BODY_H - 12)
    a(f"{x0:.2f} {y0:.2f} m {x1:.2f} {y1:.2f} l S")
    a(f"{x0:.2f} {y0 - 4:.2f} m {x0:.2f} {y0 + 4:.2f} l S")
    a(f"{x1:.2f} {y1 - 4:.2f} m {x1:.2f} {y1 + 4:.2f} l S")
    text(x0, y0 - 14, 8, "0")
    text(x1 - 12, y1 - 14, 8, "50 mm")

    a("0.2 0.2 0.2 RG")
    a("0.6 w")
    hx0, hy0 = mm_to_pt(-FRONT_HOLE_SPACING / 2, 10)
    hx1, hy1 = mm_to_pt(FRONT_HOLE_SPACING / 2, 10)
    a(f"{hx0:.2f} {hy0:.2f} m {hx1:.2f} {hy1:.2f} l S")
    text((hx0 + hx1) / 2 - 40, hy0 + 6, 7, f"{FRONT_HOLE_SPACING:.2f} mm hole spacing")

    text(*mm_to_pt(0, y_led0() + LED_WIN_ACROSS / 2), 8, "TM1637")
    text(*mm_to_pt(cover_lcd_w() / 2 + 2, y_cover_lcd0() + cover_lcd_h() / 2), 8, "LCD")
    text(*mm_to_pt(KEY_W / 2 + 2, y_cover_lcd0() - GAP_LCD_KEY - KEY_H / 2), 8, "keypad")
    text(*mm_to_pt(-MATE_W / 2 + 2, -MATE_BODY_H / 2), 8, "cover")

    text(40, PAGE_H / 2, 7, "<< off page")
    text(PAGE_W - 70, PAGE_H / 2, 7, "off page >>")

    notes = [
        "OpenSCAD: docs/mechanical/top-frame-assembly-2d.scad",
        "Assembled metal: top-frame-assembled-scan-*.png",
        "Cover plate is a NEW part (not stock metal).",
    ]
    for i, n in enumerate(notes):
        text(PAGE_W - 300, 120 - i * 11, 7, n)

    a("0.75 0.75 0.75 RG")
    a("0.5 w")
    a(f"18 18 {PAGE_W - 36} {PAGE_H - 36} re S")
    return "\n".join(lines)


def build_pdf() -> bytes:
    layers = [
        ("enclosure_mate", layer_stream_mate()),
        ("cover_plate", layer_stream_cover()),
        ("modules", layer_stream_modules()),
        ("front_strip", layer_stream_front()),
        ("bolts", layer_stream_bolts()),
        ("annotations", layer_stream_annotations()),
    ]

    objects: list[bytes] = []

    def add(obj: bytes) -> int:
        objects.append(obj)
        return len(objects)

    add(b"")
    add(b"")
    font_id = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    fontb_id = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")

    ocg_ids: list[int] = []
    for name, _ in layers:
        ocg_ids.append(
            add(
                f"<< /Type /OCG /Name ({pdf_escape(name)}) /Usage << "
                f"/Print << /PrintState /ON >> /View << /ViewState /ON >> >> >>".encode()
            )
        )

    content_parts = [
        f"/OC /OC{oid} BDC\n{stream}\nEMC" for (_, stream), oid in zip(layers, ocg_ids)
    ]
    full_stream = "\n".join(content_parts).encode()
    contents_id = add(
        f"<< /Length {len(full_stream)} >>\nstream\n".encode()
        + full_stream
        + b"\nendstream"
    )

    prop_entries = " ".join(f"/OC{oid} {oid} 0 R" for oid in ocg_ids)
    page_id = add(
        (
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_W} {PAGE_H}] "
            f"/Resources << /Font << /F1 {font_id} 0 R /F2 {fontb_id} 0 R >> "
            f"/Properties << {prop_entries} >> >> "
            f"/Contents {contents_id} 0 R >>"
        ).encode()
    )
    objects[1] = f"<< /Type /Pages /Kids [{page_id} 0 R] /Count 1 >>".encode()
    ocgs = " ".join(f"{oid} 0 R" for oid in ocg_ids)
    order = " ".join(f"{oid} 0 R" for oid in ocg_ids)
    objects[0] = (
        f"<< /Type /Catalog /Pages 2 0 R "
        f"/OCProperties << /OCGs [{ocgs}] "
        f"/D << /Order [{order}] /BaseState /ON /ListMode /VisiblePages >> >> >>"
    ).encode()

    out = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for i, obj in enumerate(objects, start=1):
        offsets.append(len(out))
        out.extend(f"{i} 0 obj\n".encode())
        out.extend(obj)
        out.extend(b"\nendobj\n")
    xref_pos = len(out)
    out.extend(f"xref\n0 {len(objects) + 1}\n".encode())
    out.extend(b"0000000000 65535 f \n")
    for off in offsets[1:]:
        out.extend(f"{off:010d} 00000 n \n".encode())
    out.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref_pos}\n%%EOF\n".encode()
    )
    return bytes(out)


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(root, "docs", "mechanical", "top-frame-assembly-2d.pdf")
    data = build_pdf()
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"Wrote {out_path} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
