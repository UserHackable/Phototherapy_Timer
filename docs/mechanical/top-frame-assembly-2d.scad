// Top-frame assembly — 2D view matching top-frame-assembled scan
//
// Sources (600 DPI; see docs/mechanical/top-frame-parts-scan.md):
//   - top-frame-assembled-scan-600dpi.png
//   - top-frame-front-strip-isolated / enclosure-mate-isolated
//   - component-modules-scan
//
// Operator stack (top → bottom) — ONE of each module:
//   1. TM1637 LED   — single module in the front-strip rectangular hole
//   2. LCD1602      — cover-plate window (upper / wider remainder of mate main hole)
//   3. 4×4 keypad   — on the cover plate (backs against plate; bottom mate slot covered)
//
// Cover plate (new part on the mate face):
//   - Covers the mate bottom slot completely
//   - Shrinks the large mate main opening, leaving a wide LCD cutout
//   - Provides a solid backer for the keypad below the LCD
//
// Metal pose (assembled scan, Y up):
//   - Orange front strip on TOP; blue mate UNDER it
//   - Shared outer bolt holes at Y = 0 on both metal parts
//   - Green cover plate on the mate face (operator side of the mate)
//
// Preview:  openscad docs/mechanical/top-frame-assembly-2d.scad
// PDF:      python3 scripts/gen-top-frame-assembly-pdf.py

/* [Layers] */
show_front = true;
show_mate = true;
show_cover = true;      // mate cover plate (keypad backer + LCD aperture)
show_modules = true;    // single TM1637, LCD, keypad
show_bolts = true;
show_page = true;
show_centerline = true;

/* [Scan-derived mm — front strip] */
front_depth = 35.3;
front_draw_len = 280;
front_hole_spacing = 63.38;
front_hole_d = 5.5;
front_hole_from_mating = 6.3;
// ONE rectangular cutout for the single TM1637 LED
led_win_along = 52.0;
led_win_across = 20.2;
led_win_from_mating = 10.5;

/* [Scan-derived mm — enclosure mate (stock metal)] */
mate_w = 88.3;
mate_body_h = 124.5;
mate_feet_extra = 8.0;
mate_hole_spacing = 63.38;
mate_hole_d = 5.5;
mate_mid_hole_d = 5.3;
// Stock main opening (large; cover plate will shrink this)
mate_main_w = 72.0;
mate_main_h = 75.0;
mate_main_from_holes = 6.0;
// Stock bottom slot (cover plate blanks this entirely)
mate_slot_w = 50.0;
mate_slot_h = 22.0;
mate_slot_from_far = 4.0;
mate_foot_w = 18.0;

/* [Cover plate — new part on mate face] */
// Plate outline = mate face (same outer size; schematic rectangle)
cover_margin = 0;           // flush to mate outer for 2D schematic
// LCD aperture left in the upper / wider portion of the stock main hole
cover_lcd_clear = 0.8;      // mm per side beyond LCD face
// Vertical placement of LCD window: top of stock main opening, down lcd height + clear
cover_lcd_from_main_top = 1.5;  // inset from top of main hole

/* [UI modules — one of each] */
tm_w = 50.0;    // single TM1637 housing face
tm_h = 18.4;
lcd_w = 71.0;   // LCD1602 bezel face (uses wider portion left by cover)
lcd_h = 24.0;
key_w = 68.3;   // 4×4 membrane outer
key_h = 75.5;
// Keypad sits on cover below LCD window
gap_lcd_key = 3.0;

/* [Page] */
page_w = 215.9;
page_h = 279.4;

/* [Hidden] */
eps = 0.05;
$fn = 48;

// ---- origin = midpoint of shared outer bolt pair ----
// X right. Y up = front free edge / LED. Y down = mate + cover + keypad.

function y_mating() = -front_hole_from_mating;
function y_free()   = front_depth - front_hole_from_mating;
function y_led0()   = y_mating() + led_win_from_mating;
function y_led1()   = y_led0() + led_win_across;
function y_led_c()  = (y_led0() + y_led1()) / 2;

// Stock mate main-opening bounds (Y decreasing downward)
function y_main1() = -mate_main_from_holes;                    // top of main hole
function y_main0() = y_main1() - mate_main_h;                  // bottom of main hole

// Cover LCD window: upper part of main hole, sized for LCD16x2 + clearance
function cover_lcd_w() = lcd_w + 2 * cover_lcd_clear;
function cover_lcd_h() = lcd_h + 2 * cover_lcd_clear;
function y_cover_lcd1() = y_main1() - cover_lcd_from_main_top;
function y_cover_lcd0() = y_cover_lcd1() - cover_lcd_h();
function y_cover_lcd_c() = (y_cover_lcd0() + y_cover_lcd1()) / 2;

// Keypad on cover below LCD window (solid backer)
function y_key1() = y_cover_lcd0() - gap_lcd_key;
function y_key0() = y_key1() - key_h;

module layer_front() {
    difference() {
        translate([-front_draw_len/2, y_mating()])
            square([front_draw_len, y_free() - y_mating()]);
        for (sx = [-1, 1])
            translate([sx * front_hole_spacing/2, 0])
                circle(d=front_hole_d);
        // single TM1637 LED rectangular hole
        translate([-led_win_along/2, y_led0()])
            square([led_win_along, led_win_across]);
    }
}

module layer_mate() {
    // Stock metal only (openings as scanned — cover plate closes what we don't need)
    left = -mate_w/2;
    difference() {
        union() {
            translate([left, -mate_body_h])
                square([mate_w, mate_body_h + eps]);
            if (mate_feet_extra > 0) {
                translate([left, 0])
                    square([mate_foot_w, mate_feet_extra]);
                translate([left + mate_w - mate_foot_w, 0])
                    square([mate_foot_w, mate_feet_extra]);
            }
        }
        for (x = [-mate_hole_spacing/2, 0, mate_hole_spacing/2])
            translate([x, 0])
                circle(d = (x == 0 ? mate_mid_hole_d : mate_hole_d));
        translate([-mate_main_w/2, y_main0()])
            square([mate_main_w, mate_main_h]);
        translate([-mate_slot_w/2, -mate_body_h + mate_slot_from_far])
            square([mate_slot_w, mate_slot_h]);
    }
}

module layer_cover() {
    // Cover plate on mate face: blanks bottom slot + shrinks main hole to LCD aperture.
    // Outer outline = mate body face (schematic rectangle).
    left = -mate_w/2;
    difference() {
        translate([left, -mate_body_h])
            square([mate_w, mate_body_h]);
        // Only opening left: LCD window (upper / wider portion of former main hole)
        translate([-cover_lcd_w()/2, y_cover_lcd0()])
            square([cover_lcd_w(), cover_lcd_h()]);
        // Bolt holes still pass through (clear cover at shared fasteners)
        for (x = [-mate_hole_spacing/2, 0, mate_hole_spacing/2])
            translate([x, 0])
                circle(d = (x == 0 ? mate_mid_hole_d : mate_hole_d) + 0.5);
    }
}

module layer_modules() {
    // Single TM1637 in front-strip LED hole
    color([0.1, 0.55, 0.95, 0.9])
        translate([-tm_w/2, y_led_c() - tm_h/2])
            square([tm_w, tm_h]);

    // Single LCD1602 in cover-plate window
    color([0.15, 0.15, 0.35, 0.9])
        translate([-lcd_w/2, y_cover_lcd_c() - lcd_h/2])
            square([lcd_w, lcd_h]);

    // Keypad on cover (solid backer — bottom mate slot is covered)
    color([0.12, 0.12, 0.12, 0.9])
        translate([-key_w/2, y_key0()])
            square([key_w, key_h]);
}

module layer_bolts() {
    for (x = [-front_hole_spacing/2, front_hole_spacing/2])
        translate([x, 0])
            circle(d=front_hole_d * 0.45);
    translate([0, 0])
        circle(d=mate_mid_hole_d * 0.45);
}

module layer_page() {
    mid_y = (y_free() + (-mate_body_h)) / 2;
    translate([-page_w/2, mid_y - page_h/2])
        difference() {
            square([page_w, page_h]);
            translate([eps, eps])
                square([page_w - 2*eps, page_h - 2*eps]);
        }
}

module layer_centerline() {
    translate([0, -mate_body_h - 10])
        square([0.15, front_depth + mate_body_h + 20]);
}

// ---- draw: mate → cover → modules → front → bolts ----
if (show_page)
    color([0.7, 0.7, 0.7, 0.4]) layer_page();
if (show_centerline)
    color([0.5, 0.5, 0.5]) layer_centerline();
if (show_mate)
    color([0.2, 0.45, 0.85, 0.45]) layer_mate();
if (show_cover)
    color([0.25, 0.7, 0.35, 0.55]) layer_cover();
if (show_modules)
    layer_modules();
if (show_front)
    color([0.9, 0.35, 0.15, 0.55]) layer_front();
if (show_bolts)
    color([0.1, 0.1, 0.1]) layer_bolts();
