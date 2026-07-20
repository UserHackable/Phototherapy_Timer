// Phototherapy Timer — front surface plate (UI faces)
//
// Operator-facing stack, vertical centerline:
//   top → LCD bezel → TM1637 housing → 4×4 keypad → bottom
//
// Display faces are COPLANAR with the keypad face (flush front plane).
// Backpacks / PCB bulk hang behind the plate (not modeled in detail yet).
//
// Dimensions from docs/front-panel.md (caliper / ruler, 2026-07).
// Z-depth of pockets: wait for better calipers.
//
// Preview:  openscad docs/mechanical/front-panel.scad
// PNG:      openscad -o /tmp/front-panel.png --imgsize=1200,900 \
//             --camera=0,0,0,55,0,25,400 docs/mechanical/front-panel.scad
// STL:      openscad -o docs/mechanical/front-panel.stl docs/mechanical/front-panel.scad

/* [Faces — measured] */
// LCD bezel face (mm)
lcd_w = 71;
lcd_h = 24;

// TM1637 LED housing face (mm) — not full PCB
tm_w = 50;
tm_h = 18.4;

// Keypad membrane outer (mm) — 2 11/16" x (3" - 1/32")
key_w = 68.3;
key_h = 75.5;

/* [Layout] */
// Gap between LCD bottom and TM1637 top (mm)
gap_lcd_tm = 10;

// Gap between TM1637 bottom and keypad top (mm)
gap_tm_key = 10;

// Margin around content to plate outer edge (mm)
margin_x = 12;
margin_top = 10;
margin_bottom = 12; // extra room for ribbon exit below keypad

/* [Plate] */
plate_thickness = 3;
// How far face inserts sit proud of plate front (0 = flush with front)
face_proud = 0;

// Cutout clearance beyond face outline (mm each side total = 2*)
cutout_clear = 0.6;

/* [Visibility] */
show_plate = true;
show_faces = true;
show_cutouts_only = false; // if true: plate with holes, no solid faces
show_centerline = true;
show_labels = true;
// Simple behind-plate blocks (illustrative; depths are placeholders)
show_back_bulk = false;
lcd_back_depth = 18; // placeholder until calipered
tm_back_depth = 12;  // placeholder
key_adapter_depth = 8; // placeholder I2C board pocket

/* [Hidden] */
eps = 0.02;
$fn = 32;

// ---- derived layout (Y up, X right, Z toward operator) ----
// Origin: plate center in X, front face at Z=0, Y=0 at vertical center of content stack.

content_h = lcd_h + gap_lcd_tm + tm_h + gap_tm_key + key_h;
content_w = max(lcd_w, tm_w, key_w);

plate_w = content_w + 2 * margin_x;
plate_h = content_h + margin_top + margin_bottom;

// Y of each face center (content centered on plate)
y_top = plate_h / 2 - margin_top;
y_lcd = y_top - lcd_h / 2;
y_tm  = y_lcd - lcd_h / 2 - gap_lcd_tm - tm_h / 2;
y_key = y_tm  - tm_h / 2 - gap_tm_key - key_h / 2;

// Plate occupies Z = [-plate_thickness, 0]; front skin at Z = 0.
// Face inserts sit on the front plane so LCD / TM / keypad are coplanar.

module face_rect(w, h, t = 1.0, color_name = "SteelBlue") {
    // Front of face at Z = face_proud + t (all faces share face_proud)
    color(color_name)
        translate([-w / 2, -h / 2, face_proud])
            cube([w, h, t]);
}

module cutout_rect(w, h) {
    cw = w + 2 * cutout_clear;
    ch = h + 2 * cutout_clear;
    translate([-cw / 2, -ch / 2, -plate_thickness - eps])
        cube([cw, ch, plate_thickness + 2 * eps]);
}

module plate_body() {
    color("Gainsboro")
        translate([-plate_w / 2, -plate_h / 2, -plate_thickness])
            cube([plate_w, plate_h, plate_thickness]);
}

module plate_with_cutouts() {
    difference() {
        plate_body();
        translate([0, y_lcd, 0]) cutout_rect(lcd_w, lcd_h);
        translate([0, y_tm, 0])  cutout_rect(tm_w, tm_h);
        translate([0, y_key, 0]) cutout_rect(key_w, key_h);
        // ribbon slot under keypad (optional service path)
        translate([-6, y_key - key_h / 2 - 12, -plate_thickness - eps])
            cube([12, 12, plate_thickness + 2 * eps]);
    }
}

module faces_flush() {
    // All three fronts even (same face_proud / thickness family)
    face_t = 1.0;
    translate([0, y_lcd, 0])
        face_rect(lcd_w, lcd_h, face_t, "RoyalBlue");
    translate([0, y_tm, 0])
        face_rect(tm_w, tm_h, face_t, "DarkRed");
    translate([0, y_key, 0])
        face_rect(key_w, key_h, face_t, "DimGray");

    // Key legend hint (optional visual only)
    if (show_labels) {
        color("SkyBlue")
            for (col = [-1.5, -0.5, 0.5, 1.5])
                for (row = [1.5, 0.5, -0.5, -1.5])
                    translate([
                        col * (key_w / 4.5) - key_w / 12,
                        y_key + row * (key_h / 4.5) - key_h / 12,
                        face_proud + face_t
                    ])
                        cube([key_w / 6, key_h / 6, 0.35]);
    }
}

module back_bulk_placeholder() {
    // Behind plate (negative Z): depths are placeholders until calipered
    color("Green", 0.45)
        translate([-(lcd_w + 6) / 2, y_lcd - (lcd_h + 4) / 2, -plate_thickness - lcd_back_depth])
            cube([lcd_w + 6, lcd_h + 4, lcd_back_depth]);
    color("Maroon", 0.45)
        translate([-(tm_w + 4) / 2, y_tm - (tm_h + 6) / 2, -plate_thickness - tm_back_depth])
            cube([tm_w + 4, tm_h + 6, tm_back_depth]);
    color("Gray", 0.35)
        translate([-15, y_key - key_h / 2 - 18, -plate_thickness - key_adapter_depth])
            cube([30, 20, key_adapter_depth]);
}

module centerline_mark() {
    color("OrangeRed")
        translate([-0.2, -plate_h / 2 + 2, face_proud + 1.1])
            cube([0.4, plate_h - 4, 0.25]);
}

// ---- assembly ----
if (show_plate) {
    if (show_cutouts_only || show_faces)
        plate_with_cutouts();
    else
        plate_body();
}

if (show_faces && !show_cutouts_only)
    faces_flush();

if (show_back_bulk)
    back_bulk_placeholder();

if (show_centerline)
    centerline_mark();

// Echo layout summary in console when rendered/previewed
echo("=== front-panel layout (mm) ===");
echo("plate_w x plate_h x t =", plate_w, plate_h, plate_thickness);
echo("content_w x content_h =", content_w, content_h);
echo("lcd center Y =", y_lcd, " size", lcd_w, "x", lcd_h);
echo("tm  center Y =", y_tm,  " size", tm_w, "x", tm_h);
echo("key center Y =", y_key, " size", key_w, "x", key_h);
echo("faces coplanar at Z~0 (even with keypad)");
