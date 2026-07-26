// Stepped Shaft with Keyway
// Total Length: 120 mm along X-axis

$fn = 64;

// --- Dimensions ---
d1 = 20;          // Diameter of first end section (mm)
l1 = 40;          // Length of first end section (mm)

d2 = 30;          // Diameter of middle section (mm)
l2 = 40;          // Length of middle section (mm)

d3 = 20;          // Diameter of second end section (mm)
l3 = 40;          // Length of second end section (mm)

chamfer = 1.0;    // End chamfer size (mm)

// Keyway dimensions (on middle section)
keyway_w = 6.0;   // Keyway width (mm)
keyway_d = 3.0;   // Keyway depth (mm)
keyway_l = 28.0;  // Keyway overall length (mm)

// --- Calculated Parameters ---
r1 = d1 / 2;
r2 = d2 / 2;
r3 = d3 / 2;

x0 = -(l1 + l2 / 2); // -60 mm
x1 = -l2 / 2;        // -20 mm
x2 = l2 / 2;         //  20 mm
x3 = l1 + l2 / 2;    //  60 mm

// --- Modules ---

module shaft_body() {
    // 2D revolution profile rotated to lie along the X-axis
    rotate([0, 90, 0])
        rotate_extrude($fn = $fn)
            polygon(points = [
                [0, x0],
                [r1 - chamfer, x0],
                [r1, x0 + chamfer],
                [r1, x1],
                [r2, x1],
                [r2, x2],
                [r3, x2],
                [r3, x3 - chamfer],
                [r3 - chamfer, x3],
                [0, x3]
            ]);
}

module keyway_cutter() {
    // Cutter for an endmill-style rounded keyway on top (+Z) of middle section
    slot_straight = keyway_l - keyway_w;
    h_cut = keyway_d + 2.0; // Overshoot above shaft surface for clean boolean
    
    translate([0, 0, r2 - keyway_d])
        hull() {
            translate([-slot_straight / 2, 0, 0])
                cylinder(d = keyway_w, h = h_cut, $fn = 32);
            translate([slot_straight / 2, 0, 0])
                cylinder(d = keyway_w, h = h_cut, $fn = 32);
        }
}

module stepped_shaft() {
    difference() {
        shaft_body();
        keyway_cutter();
    }
}

// --- Main Assembly ---
stepped_shaft();
