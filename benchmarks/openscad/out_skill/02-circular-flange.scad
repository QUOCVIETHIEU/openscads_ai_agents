// Circular Flange with Central Bore, Bolt Circle, and Filleted Edged
// All dimensions in millimeters

// Design Parameters
flange_od        = 80;   // Outer diameter of flange
flange_thickness = 10;   // Overall flange thickness
bore_id          = 30;   // Central through-bore diameter
bolt_circle_pcd  = 60;   // Pitch circle diameter (PCD) for bolt holes
bolt_hole_d      = 6;    // Diameter of bolt holes
num_bolt_holes   = 6;    // Number of bolt holes
fillet_radius    = 1.5;  // Fillet radius for outer edges

// Render resolution settings
$fn = 64;

module flange_cross_section() {
    r_inner = bore_id / 2;
    r_outer = flange_od / 2;
    h = flange_thickness;
    rf = fillet_radius;

    hull() {
        // Inner bore vertical boundary
        translate([r_inner, 0])
            square([0.01, h]);

        // Bottom-outer rounded corner
        translate([r_outer - rf, rf])
            circle(r = rf, $fn = 32);

        // Top-outer rounded corner
        translate([r_outer - rf, h - rf])
            circle(r = rf, $fn = 32);
    }
}

module flange_body() {
    rotate_extrude($fn = 120) {
        flange_cross_section();
    }
}

module bolt_holes() {
    r_bc = bolt_circle_pcd / 2;
    r_hole = bolt_hole_d / 2;
    h_cut = flange_thickness + 2; // Overshoot to ensure clean subtraction

    for (i = [0 : num_bolt_holes - 1]) {
        angle = i * (360 / num_bolt_holes);
        rotate([0, 0, angle])
            translate([r_bc, 0, -1])
                cylinder(r = r_hole, h = h_cut, $fn = 32);
    }
}

module main_assembly() {
    difference() {
        flange_body();
        bolt_holes();
    }
}

main_assembly();
