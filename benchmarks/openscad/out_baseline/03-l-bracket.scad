// L-Bracket with Base Holes, Rear Plate Holes, and Gussets
// Dimensions in millimeters

$fn = 60;

// Dimensions
width = 80;          // X dimension
base_y = 60;         // Y dimension of base plate
base_z = 6;          // Thickness of base plate
back_y = 6;          // Thickness of back plate
back_z = 40;         // Total height of back plate (Z)

hole_dia = 5;        // Diameter for mounting holes
gusset_thick = 6;    // Thickness of triangular gussets
gusset_y = 35;       // Length of gusset along Y
gusset_z = 28;       // Height of gusset along Z

hole_x1 = 20;        // X location for first set of holes
hole_x2 = 60;        // X location for second set of holes

module l_bracket() {
    difference() {
        // Main geometry: Base plate + Back plate + Gussets
        union() {
            // Base plate lying in XY plane
            cube([width, base_y, base_z]);

            // Rear vertical plate rising in +Z along the back edge
            translate([0, base_y - back_y, 0])
                cube([width, back_y, back_z]);

            // Left Gusset
            translate([8, base_y - back_y, base_z])
                rotate([90, 0, -90])
                    linear_extrude(height = gusset_thick)
                        polygon(points = [[0, 0], [gusset_y, 0], [0, gusset_z]]);

            // Right Gusset
            translate([width - 8 - gusset_thick, base_y - back_y, base_z])
                rotate([90, 0, -90])
                    linear_extrude(height = gusset_thick)
                        polygon(points = [[0, 0], [gusset_y, 0], [0, gusset_z]]);
        }

        // Vertical holes in the base plate
        translate([hole_x1, (base_y - back_y) / 2, -1])
            cylinder(d = hole_dia, h = base_z + 2);

        translate([hole_x2, (base_y - back_y) / 2, -1])
            cylinder(d = hole_dia, h = base_z + 2);

        // Horizontal holes in the back plate
        translate([hole_x1, base_y + 1, (back_z + base_z) / 2])
            rotate([90, 0, 0])
                cylinder(d = hole_dia, h = back_y + 2);

        translate([hole_x2, base_y + 1, (back_z + base_z) / 2])
            rotate([90, 0, 0])
                cylinder(d = hole_dia, h = back_y + 2);
    }
}

l_bracket();
