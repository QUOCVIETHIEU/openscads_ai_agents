// Circular Flange with Bolt Holes
// Units in millimeters

// --- Parameters ---
flange_dia       = 80;    // Outer diameter of the flange
flange_thickness = 10;    // Thickness of the flange
bore_dia         = 30;    // Central through-bore diameter
bolt_circle_dia  = 60;    // Diameter of the bolt hole circle
bolt_hole_dia    = 6;     // Diameter of each bolt hole
num_bolt_holes   = 6;     // Number of bolt holes
fillet_radius    = 1.5;   // Radius for outer edge fillet

// Quality resolution
$fn = 120;

// --- Modules ---
module flange_cross_section() {
    r_outer = flange_dia / 2;
    r_fillet = fillet_radius;
    
    hull() {
        // Main inner rectangle (up to fillet start)
        square([r_outer - r_fillet, flange_thickness]);
        
        // Bottom-outer rounded corner
        translate([r_outer - r_fillet, r_fillet])
            circle(r = r_fillet);
            
        // Top-outer rounded corner
        translate([r_outer - r_fillet, flange_thickness - r_fillet])
            circle(r = r_fillet);
    }
}

module flange() {
    difference() {
        // Solid revolute flange body with rounded outer edges
        rotate_extrude()
            flange_cross_section();
        
        // Central bore
        translate([0, 0, -1])
            cylinder(d = bore_dia, h = flange_thickness + 2);
        
        // Bolt holes array
        for (i = [0 : num_bolt_holes - 1]) {
            rotate([0, 0, i * (360 / num_bolt_holes)])
                translate([bolt_circle_dia / 2, 0, -1])
                    cylinder(d = bolt_hole_dia, h = flange_thickness + 2);
        }
    }
}

// --- Render Model ---
flange();
