#!/usr/bin/env python3
"""Generate 1:1 printable cardboard cut template for the front panel.

Output: docs/mechanical/front-panel-cardboard.pdf

Matches defaults in docs/mechanical/front-panel.scad:
  top → TM1637 50x18.4 → LCD 71x24 → keypad 68.3x75.5 → bottom; gaps 10 mm.

Print at 100% / Actual size (no fit-to-page). Verify the 100 mm scale bar
with a ruler before cutting.

Page 1: plate outline + openings (cut template)
Page 2: face-only outlines for dry-fit of parts (no plate border required)
"""
from __future__ import annotations

import os
import sys

PAGE_W, PAGE_H = 612.0, 792.0  # US Letter points
MM = 72.0 / 25.4

# Keep in sync with docs/mechanical/front-panel.scad
# Order: top → TM1637 → LCD → keypad → bottom
TM_W, TM_H = 50.0, 18.4
LCD_W, LCD_H = 71.0, 24.0
KEY_W, KEY_H = 68.3, 75.5
GAP_TM_LCD = 10.0
GAP_LCD_KEY = 10.0
MARGIN_X = 12.0
MARGIN_TOP = 10.0
MARGIN_BOTTOM = 12.0
CUTOUT_CLEAR = 0.6  # mm added to each side of face for cardboard openings


def pdf_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def layout():
    content_h = TM_H + GAP_TM_LCD + LCD_H + GAP_LCD_KEY + KEY_H
    content_w = max(LCD_W, TM_W, KEY_W)
    plate_w = content_w + 2 * MARGIN_X
    plate_h = content_h + MARGIN_TOP + MARGIN_BOTTOM
    # centers relative to plate center (same as OpenSCAD)
    y_top = plate_h / 2 - MARGIN_TOP
    y_tm = y_top - TM_H / 2
    y_lcd = y_tm - TM_H / 2 - GAP_TM_LCD - LCD_H / 2
    y_key = y_lcd - LCD_H / 2 - GAP_LCD_KEY - KEY_H / 2
    return {
        "content_w": content_w,
        "content_h": content_h,
        "plate_w": plate_w,
        "plate_h": plate_h,
        "y_tm": y_tm,
        "y_lcd": y_lcd,
        "y_key": y_key,
    }


def build_pdf(streams: list[bytes]) -> bytes:
    objects: list[bytes] = []

    def add(obj: bytes) -> int:
        objects.append(obj)
        return len(objects)

    add(b"")
    add(b"")
    font_id = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    fontb_id = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")

    page_ids: list[int] = []
    for stream in streams:
        contents = (
            f"<< /Length {len(stream)} >>\nstream\n".encode()
            + stream
            + b"\nendstream"
        )
        contents_id = add(contents)
        page_body = (
            f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {PAGE_W} {PAGE_H}] "
            f"/Resources << /Font << /F1 {font_id} 0 R /F2 {fontb_id} 0 R >> >> "
            f"/Contents {contents_id} 0 R >>"
        ).encode()
        page_ids.append(add(page_body))

    kids = " ".join(f"{pid} 0 R" for pid in page_ids)
    objects[1] = f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>".encode()
    objects[0] = b"<< /Type /Catalog /Pages 2 0 R >>"

    out = bytearray()
    out.extend(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
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


class Drawer:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def a(self, s: str) -> None:
        self.lines.append(s)

    def stroke_rgb(self, r: float, g: float, b: float, w: float) -> None:
        self.a(f"{r:.3f} {g:.3f} {b:.3f} RG")
        self.a(f"{w:.2f} w")

    def fill_rgb(self, r: float, g: float, b: float) -> None:
        self.a(f"{r:.3f} {g:.3f} {b:.3f} rg")

    def line(self, x1: float, y1: float, x2: float, y2: float) -> None:
        self.a(f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S")

    def rect(self, x: float, y: float, w: float, h: float, fill: bool = False) -> None:
        op = "B" if fill else "S"
        self.a(f"{x:.2f} {y:.2f} {w:.2f} {h:.2f} re {op}")

    def rect_c(self, cx: float, cy: float, w: float, h: float, fill: bool = False) -> None:
        self.rect(cx - w / 2, cy - h / 2, w, h, fill=fill)

    def text(self, x: float, y: float, s: str, size: float = 8, bold: bool = False) -> None:
        font = "F2" if bold else "F1"
        self.a("BT")
        self.a(f"/{font} {size:.1f} Tf")
        self.a(f"1 0 0 1 {x:.2f} {y:.2f} Tm")
        self.a(f"({pdf_escape(s)}) Tj")
        self.a("ET")

    def dash(self, on: float = 3, off: float = 2) -> None:
        self.a(f"[{on:.1f} {off:.1f}] 0 d")

    def solid(self) -> None:
        self.a("[] 0 d")

    def done(self) -> bytes:
        return "\n".join(self.lines).encode("latin-1", errors="replace")


def page_cut_template(L: dict) -> bytes:
    d = Drawer()
    # Place plate centered on page, slightly high for title room
    plate_w_pt = L["plate_w"] * MM
    plate_h_pt = L["plate_h"] * MM
    cx = PAGE_W / 2
    # bottom of plate
    cy = 72 + plate_h_pt / 2  # ~1" from bottom
    # If plate is tall, shift up a bit if needed
    max_top = PAGE_H - 90
    if cy + plate_h_pt / 2 > max_top:
        cy = max_top - plate_h_pt / 2

    def to_page(lx: float, ly: float) -> tuple[float, float]:
        """Local plate coords (center origin, Y up) -> PDF points."""
        return cx + lx * MM, cy + ly * MM

    # Title
    d.fill_rgb(0.1, 0.1, 0.1)
    d.text(36, PAGE_H - 28, "Phototherapy Timer - cardboard cut template (1:1)", size=12, bold=True)
    d.text(
        36,
        PAGE_H - 42,
        "PRINT AT 100% / Actual size - do NOT fit-to-page. Solid = cut. Dashed = fold/mark only.",
        size=8,
    )
    d.text(
        36,
        PAGE_H - 54,
        (
            f"Plate {L['plate_w']:.1f} x {L['plate_h']:.1f} mm  |  "
            f"TM {TM_W:g}x{TM_H:g}  LCD {LCD_W:g}x{LCD_H:g}  Key {KEY_W:g}x{KEY_H:g}  |  "
            f"gaps {GAP_TM_LCD:g}/{GAP_LCD_KEY:g} mm  |  openings +{CUTOUT_CLEAR:g} mm clear"
        ),
        size=7,
    )

    # Scale bar (100 mm) - top left of content area
    bar_x, bar_y = 36, PAGE_H - 78
    d.stroke_rgb(0, 0, 0, 1.2)
    d.line(bar_x, bar_y, bar_x + 100 * MM, bar_y)
    d.line(bar_x, bar_y - 4, bar_x, bar_y + 4)
    d.line(bar_x + 100 * MM, bar_y - 4, bar_x + 100 * MM, bar_y + 4)
    for i in range(0, 101, 10):
        h = 6 if i % 50 == 0 else 3
        d.line(bar_x + i * MM, bar_y, bar_x + i * MM, bar_y + h)
    d.fill_rgb(0.15, 0.15, 0.15)
    d.text(bar_x, bar_y + 10, "100 mm scale - must measure 100.0 mm on print", size=7)

    # Outer plate - solid heavy cut
    px, py = to_page(0, 0)
    d.stroke_rgb(0, 0, 0, 1.5)
    d.solid()
    d.rect_c(px, py, plate_w_pt, plate_h_pt)

    # Centerline - dashed mark
    d.stroke_rgb(0.8, 0.2, 0.1, 0.6)
    d.dash(2, 2)
    x0, y0 = to_page(0, -L["plate_h"] / 2 + 2)
    x1, y1 = to_page(0, L["plate_h"] / 2 - 2)
    d.line(x0, y0, x1, y1)
    d.solid()

    faces = [
        ("TM1637", TM_W, TM_H, L["y_tm"], (0.6, 0.1, 0.1)),
        ("LCD bezel", LCD_W, LCD_H, L["y_lcd"], (0.1, 0.2, 0.7)),
        ("Keypad", KEY_W, KEY_H, L["y_key"], (0.15, 0.15, 0.15)),
    ]

    for name, fw, fh, ly, rgb in faces:
        # Opening with clearance (what to cut out of cardboard)
        ow = (fw + 2 * CUTOUT_CLEAR) * MM
        oh = (fh + 2 * CUTOUT_CLEAR) * MM
        fx, fy = to_page(0, ly)
        d.stroke_rgb(*rgb, 1.2)
        d.solid()
        d.rect_c(fx, fy, ow, oh)
        # True face size inside (dashed) so parts can be centered in opening
        d.dash(2, 1.5)
        d.stroke_rgb(*rgb, 0.6)
        d.rect_c(fx, fy, fw * MM, fh * MM)
        d.solid()
        d.fill_rgb(*rgb)
        d.text(fx - 28, fy - 3, name, size=7, bold=True)
        d.fill_rgb(0.2, 0.2, 0.2)
        d.text(
            fx + ow / 2 + 4,
            fy - 2,
            f"{fw:g}x{fh:g} (+{CUTOUT_CLEAR:g})",
            size=6,
        )

    # Ribbon slot under keypad
    slot_w, slot_h = 12.0, 12.0
    sx, sy = to_page(0, L["y_key"] - KEY_H / 2 - 6)
    d.stroke_rgb(0.3, 0.3, 0.3, 1.0)
    d.rect_c(sx, sy, slot_w * MM, slot_h * MM)
    d.fill_rgb(0.3, 0.3, 0.3)
    d.text(sx - 22, sy - slot_h * MM / 2 - 10, "ribbon slot", size=6)

    # Dimension callouts
    d.fill_rgb(0.1, 0.1, 0.1)
    left_x = cx - plate_w_pt / 2 - 4
    d.text(36, 48, "Instructions:", size=8, bold=True)
    d.text(36, 36, "1) Confirm 100 mm bar.  2) Cut outer plate (heavy black).  3) Cut solid openings (LCD/TM/keypad).", size=7)
    d.text(36, 24, "4) Dashed inner rects = exact face size (optional mark).  5) Tape/glue parts from behind; faces flush to front.", size=7)
    d.text(36, 12, "Matches docs/mechanical/front-panel.scad  |  regenerate: python3 scripts/gen-front-panel-cardboard.py", size=6)

    # Plate size label
    d.text(cx - 40, cy + plate_h_pt / 2 + 8, f"outer {L['plate_w']:.1f} x {L['plate_h']:.1f} mm", size=8, bold=True)

    return d.done()


def page_faces_only(L: dict) -> bytes:
    """Page 2: stack of faces only for placing real parts (no plate frame required)."""
    d = Drawer()
    d.fill_rgb(0.1, 0.1, 0.1)
    d.text(36, PAGE_H - 28, "Face outlines only - dry-fit parts (1:1)", size=12, bold=True)
    d.text(
        36,
        PAGE_H - 42,
        "Same sizes/gaps as cut template. PRINT 100%. Place TM1637, LCD, keypad on dashed boxes.",
        size=8,
    )

    # Scale bar
    bar_x, bar_y = 36, PAGE_H - 70
    d.stroke_rgb(0, 0, 0, 1.2)
    d.line(bar_x, bar_y, bar_x + 100 * MM, bar_y)
    d.line(bar_x, bar_y - 4, bar_x, bar_y + 4)
    d.line(bar_x + 100 * MM, bar_y - 4, bar_x + 100 * MM, bar_y + 4)
    d.fill_rgb(0.15, 0.15, 0.15)
    d.text(bar_x, bar_y + 8, "100 mm", size=7)

    content_h = L["content_h"]
    cx = PAGE_W / 2
    # top of stack near top of page
    top_y = PAGE_H - 100
    # map plate Y so content top (TM top) -> top_y
    y_stack_top = L["y_tm"] + TM_H / 2

    def to_page(lx: float, ly: float) -> tuple[float, float]:
        return cx + lx * MM, top_y - (y_stack_top - ly) * MM

    # light plate footprint dashed
    d.dash(2, 2)
    d.stroke_rgb(0.7, 0.7, 0.7, 0.8)
    pw, ph = L["plate_w"] * MM, L["plate_h"] * MM
    # center of plate in this mapping
    pcx, pcy = to_page(0, 0)
    d.rect_c(pcx, pcy, pw, ph)
    d.solid()

    faces = [
        ("TM1637 50 x 18.4 mm", TM_W, TM_H, L["y_tm"], (0.7, 0.15, 0.1)),
        ("LCD bezel 71 x 24 mm", LCD_W, LCD_H, L["y_lcd"], (0.1, 0.25, 0.7)),
        ("Keypad 68.3 x 75.5 mm", KEY_W, KEY_H, L["y_key"], (0.1, 0.45, 0.2)),
    ]
    for name, fw, fh, ly, rgb in faces:
        fx, fy = to_page(0, ly)
        d.dash(3, 2)
        d.stroke_rgb(*rgb, 1.3)
        d.rect_c(fx, fy, fw * MM, fh * MM)
        d.solid()
        d.fill_rgb(*rgb)
        d.text(fx - 40, fy - 3, name, size=8, bold=True)

    # centerline
    d.dash(1, 2)
    d.stroke_rgb(0.8, 0.3, 0.1, 0.5)
    x0, y0 = to_page(0, L["y_key"] - KEY_H / 2 - 5)
    x1, y1 = to_page(0, L["y_tm"] + TM_H / 2 + 5)
    d.line(x0, y0, x1, y1)
    d.solid()

    d.fill_rgb(0.2, 0.2, 0.2)
    d.text(36, 30, "Use this page to check part placement before cutting the plate on page 1.", size=8)
    d.text(36, 16, "Order: TM1637 → LCD → keypad. Gaps 10 mm. Faces coplanar on finished plate.", size=7)

    return d.done()


def main() -> int:
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(repo, "docs", "mechanical", "front-panel-cardboard.pdf")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    L = layout()
    pdf = build_pdf([page_cut_template(L), page_faces_only(L)])
    with open(out_path, "wb") as f:
        f.write(pdf)

    print(f"Wrote {out_path} ({len(pdf)} bytes)")
    print(
        f"Plate {L['plate_w']:.1f} x {L['plate_h']:.1f} mm  "
        f"content {L['content_w']:.1f} x {L['content_h']:.1f} mm"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
