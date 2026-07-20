#!/usr/bin/env python3
"""Generate US Letter mm graph paper PDF for front-panel dry-fit.

Output: docs/mechanical/letter-graph-paper-mm.pdf

Page 1: blank 1 mm / 5 mm / 10 mm grid (place parts, read sizes).
Page 2: same grid + true-size dashed outlines from docs/front-panel.md.

Print at 100% / Actual size (disable fit-to-page). Verify 100 mm ticks with a ruler.
"""
from __future__ import annotations

import os
import sys

# US Letter in points (1 pt = 1/72 in)
PAGE_W, PAGE_H = 612.0, 792.0
MM = 72.0 / 25.4  # points per mm

# Measured faces (docs/front-panel.md) — update when calipers change
LCD_W, LCD_H = 71.0, 24.0
TM_W, TM_H = 50.0, 18.4
KEY_W, KEY_H = 68.3, 75.5  # 2 11/16 in x (3 in - 1/32 in)
STACK_GAP_MM = 5.0


def pdf_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def content_stream(
    page_kind: str,
    origin_x: float,
    origin_y: float,
    cols_mm: int,
    rows_mm: int,
    grid_w: float,
    grid_h: float,
    margin_left: float,
) -> bytes:
    lines: list[str] = []
    a = lines.append

    a("BT")
    a("/F1 11 Tf")
    a(f"1 0 0 1 {margin_left:.2f} {PAGE_H - 0.32 * 72:.2f} Tm")
    if page_kind == "blank":
        a(f"({pdf_escape('Phototherapy Timer - letter graph paper (mm)')}) Tj")
    else:
        a(f"({pdf_escape('Front panel faces - true-size outlines (measured)')}) Tj")
    a("ET")

    a("BT")
    a("/F1 8 Tf")
    a(f"1 0 0 1 {margin_left:.2f} {PAGE_H - 0.48 * 72:.2f} Tm")
    if page_kind == "blank":
        sub = (
            f"US Letter | 1 mm / 5 mm / 10 mm | grid {cols_mm}x{rows_mm} mm | "
            "origin bottom-left | PRINT AT 100% (Actual size, no fit-to-page)"
        )
    else:
        sub = (
            "Dashed boxes at measured face sizes | LCD / TM1637 / keypad centerline | "
            "PRINT AT 100% | overlay real parts to verify scale"
        )
    a(f"({pdf_escape(sub)}) Tj")
    a("ET")

    def stroke_rgb(r: float, g: float, b: float, w: float) -> None:
        a(f"{r:.3f} {g:.3f} {b:.3f} RG")
        a(f"{w:.2f} w")

    def line(x1: float, y1: float, x2: float, y2: float) -> None:
        a(f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S")

    def rect(x: float, y: float, w: float, h: float) -> None:
        a(f"{x:.2f} {y:.2f} {w:.2f} {h:.2f} re S")

    for i in range(cols_mm + 1):
        x = origin_x + i * MM
        if i % 10 == 0:
            stroke_rgb(0.35, 0.35, 0.35, 0.7)
        elif i % 5 == 0:
            stroke_rgb(0.55, 0.55, 0.55, 0.45)
        else:
            stroke_rgb(0.78, 0.78, 0.78, 0.25)
        line(x, origin_y, x, origin_y + grid_h)

    for j in range(rows_mm + 1):
        y = origin_y + j * MM
        if j % 10 == 0:
            stroke_rgb(0.35, 0.35, 0.35, 0.7)
        elif j % 5 == 0:
            stroke_rgb(0.55, 0.55, 0.55, 0.45)
        else:
            stroke_rgb(0.78, 0.78, 0.78, 0.25)
        line(origin_x, y, origin_x + grid_w, y)

    a("0.15 0.15 0.15 rg")
    a("/F1 6 Tf")
    for i in range(0, cols_mm + 1, 10):
        x = origin_x + i * MM
        a("BT")
        a(f"1 0 0 1 {x - 6:.2f} {origin_y - 10:.2f} Tm")
        a(f"({i}) Tj")
        a("ET")
        a("BT")
        a(f"1 0 0 1 {x - 6:.2f} {origin_y + grid_h + 3:.2f} Tm")
        a(f"({i}) Tj")
        a("ET")

    for j in range(0, rows_mm + 1, 10):
        y = origin_y + j * MM
        a("BT")
        a(f"1 0 0 1 {origin_x - 14:.2f} {y - 2:.2f} Tm")
        a(f"({j}) Tj")
        a("ET")
        a("BT")
        a(f"1 0 0 1 {origin_x + grid_w + 3:.2f} {y - 2:.2f} Tm")
        a(f"({j}) Tj")
        a("ET")

    a("0.1 0.1 0.5 rg")
    a("BT")
    a("/F1 7 Tf")
    a(f"1 0 0 1 {origin_x + 2:.2f} {origin_y + 2:.2f} Tm")
    a(f"({pdf_escape('0,0 mm')}) Tj")
    a("ET")

    a("0.3 0.3 0.3 rg")
    a("BT")
    a("/F1 7 Tf")
    a(f"1 0 0 1 {margin_left:.2f} {0.22 * 72:.2f} Tm")
    if page_kind == "blank":
        foot = (
            "Place parts face-up on the grid. "
            "Check scale: 100 mm ticks must be exactly 100 mm on a ruler."
        )
    else:
        foot = (
            f"{STACK_GAP_MM:g} mm gaps between faces. "
            "Overlay real parts on dashed boxes. See docs/front-panel.md."
        )
    a(f"({pdf_escape(foot)}) Tj")
    a("ET")

    if page_kind == "outlines":
        stack = [
            (f"LCD bezel {LCD_W:g} x {LCD_H:g} mm", LCD_W, LCD_H, (0.10, 0.25, 0.65)),
            (f"TM1637 housing {TM_W:g} x {TM_H:g} mm", TM_W, TM_H, (0.70, 0.15, 0.10)),
            (f"Keypad {KEY_W:g} x {KEY_H:g} mm", KEY_W, KEY_H, (0.05, 0.45, 0.20)),
        ]
        cx = origin_x + grid_w / 2
        y_cursor = origin_y + grid_h - 15 * MM
        a("[3 2] 0 d")
        for name, w_mm, h_mm, rgb in stack:
            w = w_mm * MM
            h = h_mm * MM
            x = cx - w / 2
            y = y_cursor - h
            r, g, b = rgb
            stroke_rgb(r, g, b, 1.2)
            rect(x, y, w, h)
            a("[] 0 d")
            a(f"{r:.3f} {g:.3f} {b:.3f} rg")
            a("BT")
            a("/F1 8 Tf")
            a(f"1 0 0 1 {cx - 55:.2f} {y + h / 2 - 3:.2f} Tm")
            a(f"({pdf_escape(name)}) Tj")
            a("ET")
            a("[3 2] 0 d")
            y_cursor = y - STACK_GAP_MM * MM

        a("[1 3] 0 d")
        stroke_rgb(0.5, 0.5, 0.5, 0.5)
        line(cx, origin_y + 5 * MM, cx, origin_y + grid_h - 5 * MM)
        a("[] 0 d")
        a("0.4 0.4 0.4 rg")
        a("BT")
        a("/F1 7 Tf")
        a(f"1 0 0 1 {cx - 20:.2f} {origin_y + 2 * MM:.2f} Tm")
        a(f"({pdf_escape('centerline')}) Tj")
        a("ET")

    return "\n".join(lines).encode("latin-1", errors="replace")


def build_pdf(streams: list[bytes]) -> bytes:
    objects: list[bytes] = []

    def add(obj: bytes) -> int:
        objects.append(obj)
        return len(objects)

    add(b"")  # 1 catalog
    add(b"")  # 2 pages
    font_id = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")

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
            f"/Resources << /Font << /F1 {font_id} 0 R >> >> "
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


def main() -> int:
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(repo, "docs", "mechanical", "letter-graph-paper-mm.pdf")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    margin_left = 0.6 * 72
    margin_right = 0.45 * 72
    margin_top = 0.7 * 72
    margin_bottom = 0.5 * 72
    origin_x = margin_left
    origin_y = margin_bottom
    cols_mm = int((PAGE_W - margin_left - margin_right) / MM)
    rows_mm = int((PAGE_H - margin_top - margin_bottom) / MM)
    grid_w = cols_mm * MM
    grid_h = rows_mm * MM

    kwargs = dict(
        origin_x=origin_x,
        origin_y=origin_y,
        cols_mm=cols_mm,
        rows_mm=rows_mm,
        grid_w=grid_w,
        grid_h=grid_h,
        margin_left=margin_left,
    )
    pdf = build_pdf(
        [
            content_stream("blank", **kwargs),
            content_stream("outlines", **kwargs),
        ]
    )
    with open(out_path, "wb") as f:
        f.write(pdf)
    print(f"Wrote {out_path} ({len(pdf)} bytes)")
    print(f"Grid: {cols_mm} mm x {rows_mm} mm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
