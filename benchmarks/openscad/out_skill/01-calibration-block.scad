// Rectangular Block with Top Perimeter Chamfer and Holes
// Units in millimeters

$fn = 64;

// Main Dimensions
length = 100;
width = 60;
height = 20;

// Chamfer Dimension
chamfer = 2;

// Hole Placement and Dimensions
hole_d = 8;
hole_x = 35;
hole_y = 20;

module main_block() {
    hull() {
        // Lower portion of the block up to the chamfer start height
        translate([-length / 2, -width / 2, 0])
            cube([length, width, height - chamfer]);
        
        // Reduced top face producing a 2mm chamfer on all top perimeter edges
        translate([-(length / 2 - chamfer), -(width / 2 - chamfer), height - 0.01])
            cube([length - 2 * chamfer, width - 2 * chamfer, 0.01]);
    }
}

module holes() {
    for (x = [-hole_x, hole_x]) {
        for (y = [-hole_y, hole_y]) {
            translate([x, y, -1])
                cylinder(d = hole_d, h = height + 2);
        }
    }
}

module part() {
    difference() {
        main_block();
        holes();
    }
}

part();
