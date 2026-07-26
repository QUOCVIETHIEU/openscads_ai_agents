// Enclosure Parameters
$fn = 64;

outer_length = 120;
outer_width  = 80;
total_height = 35;

wall_thickness  = 3;
floor_thickness = 3;
corner_radius   = 2;

// Standoff Parameters
standoff_od     = 8;     // Outer diameter of standoff
standoff_height = 15;    // Standoff height from top of floor
hole_diameter   = 2.5;   // Blind hole diameter
hole_depth      = 10;    // Depth of blind hole

// Derived Dimensions
inner_length = outer_length - 2 * wall_thickness;
inner_width  = outer_width - 2 * wall_thickness;
inner_height = total_height - floor_thickness;

standoff_r = standoff_od / 2;
hole_r     = hole_diameter / 2;

// Position standoffs flush against internal corner walls
standoff_x = (outer_length / 2) - wall_thickness - standoff_r;
standoff_y = (outer_width / 2) - wall_thickness - standoff_r;

module 2d_outer_profile() {
    offset(r = corner_radius) {
        square([outer_length - 2 * corner_radius, outer_width - 2 * corner_radius], center = true);
    }
}

module enclosure() {
    difference() {
        union() {
            // Main Hollow Shell
            difference() {
                linear_extrude(height = total_height) {
                    2d_outer_profile();
                }
                
                // Open-top inner cavity
                translate([0, 0, floor_thickness]) {
                    linear_extrude(height = inner_height + 1) {
                        square([inner_length, inner_width], center = true);
                    }
                }
            }

            // Internal Corner Standoffs
            for (sx = [-1, 1]) {
                for (sy = [-1, 1]) {
                    translate([sx * standoff_x, sy * standoff_y, floor_thickness]) {
                        cylinder(r = standoff_r, h = standoff_height);
                    }
                }
            }
        }

        // Blind holes in standoffs
        for (sx = [-1, 1]) {
            for (sy = [-1, 1]) {
                translate([sx * standoff_x, sy * standoff_y, floor_thickness + standoff_height - hole_depth]) {
                    cylinder(r = hole_r, h = hole_depth + 0.1);
                }
            }
        }
    }
}

enclosure();
