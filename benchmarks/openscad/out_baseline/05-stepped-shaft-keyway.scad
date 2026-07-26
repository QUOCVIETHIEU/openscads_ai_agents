// Stepped Shaft with Keyway
// Total Length: 120 mm along X axis

$fn = 120;

// Section Dimensions
d1 = 20;          // Diameter section 1 (mm)
l1 = 40;          // Length section 1 (mm)

d2 = 30;          // Diameter section 2 (mm)
l2 = 40;          // Length section 2 (mm)

d3 = 20;          // Diameter section 3 (mm)
l3 = 40;          // Length section 3 (mm)

chamfer = 1.5;    // End chamfer size (mm)

// Keyway Dimensions (DIN 6885 standard style for 30mm shaft)
key_w = 6;        // Keyway width (mm)
key_d = 3.5;      // Keyway depth (mm)
key_l = 30;       // Keyway length (mm)

// Radii calculations
r1 = d1 / 2;
r2 = d2 / 2;
r3 = d3 / 2;
total_length = l1 + l2 + l3;

module shaft_body() {
    // 2D revolution profile (radius vs length along shaft axis)
    profile_points = [
        [0, 0],
        [r1 - chamfer, 0],
        [r1, chamfer],
        [r1, l1],
        [r2, l1],
        [r2, l1 + l2],
        [r3, l1 + l2],
        [r3, total_length - chamfer],
        [r3 - chamfer, total_length],
        [0, total_length]
    ];
    
    // Extrude around Z axis, then rotate so shaft lies along X axis
    rotate([0, 90, 0])
        rotate_extrude($fn = $fn)
            polygon(profile_points);
}

module keyway_cutter() {
    x_center = l1 + l2 / 2;
    x_start  = x_center - key_l / 2 + key_w / 2;
    x_end    = x_center + key_l / 2 - key_w / 2;
    z_bottom = r2 - key_d;
    
    // Rounded-end slot cutout on top of middle section
    hull() {
        translate([x_start, 0, z_bottom])
            cylinder(r = key_w / 2, h = key_d + 5, $fn = 36);
        translate([x_end, 0, z_bottom])
            cylinder(r = key_w / 2, h = key_d + 5, $fn = 36);
    }
}

// Final Assembly
difference() {
    shaft_body();
    keyway_cutter();
}
