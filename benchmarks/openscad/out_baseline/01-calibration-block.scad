// Rectangular Block with Top Perimeter Chamfer and Holes
// All dimensions in millimeters

// Block Dimensions
length = 100;       // X-axis length
width = 60;         // Y-axis width
height = 20;        // Z-axis height
chamfer = 2;        // Top outer perimeter chamfer size

// Hole Parameters
hole_diameter = 8;  // Through-hole diameter
hole_radius = hole_diameter / 2;
hole_x_offset = 35; // Hole center distance from origin in X
hole_y_offset = 20; // Hole center distance from origin in Y

// Smoothness
$fn = 64;

module main_block() {
    // Base rectangular section (Z = 0 to height - chamfer)
    translate([0, 0, 0])
        linear_extrude(height = height - chamfer)
            square([length, width], center = true);
    
    // Top chamfered section (Z = height - chamfer to height)
    translate([0, 0, height - chamfer])
        linear_extrude(
            height = chamfer,
            scale = [
                (length - 2 * chamfer) / length,
                (width - 2 * chamfer) / width
            ]
        )
            square([length, width], center = true);
}

module hole_cutouts() {
    // Four vertical through-holes
    for (x = [-hole_x_offset, hole_x_offset]) {
        for (y = [-hole_y_offset, hole_y_offset]) {
            translate([x, y, -1])
                cylinder(r = hole_radius, h = height + 2);
        }
    }
}

// Final Assembly
difference() {
    main_block();
    hole_cutouts();
}
