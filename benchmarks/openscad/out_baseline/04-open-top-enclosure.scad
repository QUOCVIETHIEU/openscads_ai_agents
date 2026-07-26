// Open-top rectangular enclosure with rounded corners and mounting standoffs
// Dimensions: 120 x 80 x 35 mm

$fn = 60;

// Main Enclosure Dimensions
outer_length = 120;
outer_width  = 80;
total_height = 35;
corner_radius = 2;

// Wall and Floor Thicknesses
wall_thickness  = 3;
floor_thickness = 3;

// Internal Standoff Dimensions
standoff_od     = 7;    // Outer diameter of standoffs
hole_diameter   = 2.5;  // Blind hole diameter
hole_depth      = 8;    // Depth of blind hole
standoff_height = 10;   // Height of standoff above floor

// Calculated Inner Dimensions
inner_length = outer_length - 2 * wall_thickness;
inner_width  = outer_width  - 2 * wall_thickness;

// Positions for internal corner standoffs
standoff_x_off = wall_thickness + standoff_od / 2;
standoff_y_off = wall_thickness + standoff_od / 2;

standoff_positions = [
    [standoff_x_off, standoff_y_off],
    [outer_length - standoff_x_off, standoff_y_off],
    [outer_length - standoff_x_off, outer_width - standoff_y_off],
    [standoff_x_off, outer_width - standoff_y_off]
];

// Helper module for rounded 2D rectangle
module rounded_rect_2d(l, w, r) {
    hull() {
        translate([r, r]) circle(r = r);
        translate([l - r, r]) circle(r = r);
        translate([l - r, w - r]) circle(r = r);
        translate([r, w - r]) circle(r = r);
    }
}

// Main Assembly
difference() {
    union() {
        // Outer Shell with Hollow Interior
        difference() {
            // Solid Rounded Outer Box
            linear_extrude(height = total_height) {
                rounded_rect_2d(outer_length, outer_width, corner_radius);
            }
            
            // Hollow Cavity
            translate([wall_thickness, wall_thickness, floor_thickness]) {
                cube([inner_length, inner_width, total_height - floor_thickness + 1]);
            }
        }
        
        // Add Standoff Columns
        for (pos = standoff_positions) {
            translate([pos[0], pos[1], floor_thickness]) {
                cylinder(d = standoff_od, h = standoff_height);
            }
        }
    }
    
    // Subtract Centered Blind Holes in Standoffs
    for (pos = standoff_positions) {
        translate([pos[0], pos[1], floor_thickness + standoff_height - hole_depth + 0.01]) {
            cylinder(d = hole_diameter, h = hole_depth);
        }
    }
}
