// OpenSCAD L-Bracket with Gussets and Mounting Holes
// Base Plate: 80 x 60 x 6 mm
// Rear Vertical Plate: 80 x 40 x 6 mm

$fn = 64;

// Base Plate Dimensions
base_x = 80;
base_y = 60;
base_z = 6;

// Rear Vertical Plate Dimensions
back_x = 80;
back_y = 6;
back_z = 40;

// Mounting Hole Diameter
hole_d = 5;

// Base Plate Hole Positions
base_hole_x_offset = 22;
base_hole_y_pos = 25;

// Back Plate Hole Positions
back_hole_x_offset = 22;
back_hole_z_pos = 26; // Height above origin (middle of vertical plate)

// Gusset Stiffener Dimensions
gusset_thick = 6;
gusset_len_y = 30; // Length along base plate from back wall
gusset_len_z = 28; // Height along back wall from base plate top
gusset_x_pos = 33; // Offset from center along X axis

module gusset() {
    y0 = base_y - back_y - gusset_len_y;
    y1 = base_y - back_y;
    z0 = base_z;
    z1 = base_z + gusset_len_z;
    t  = gusset_thick;
    
    // Polyhedron forming a right-triangular prism
    points = [
        [0, y0, z0], // 0: Front bottom left
        [t, y0, z0], // 1: Front bottom right
        [0, y1, z0], // 2: Rear bottom left
        [t, y1, z0], // 3: Rear bottom right
        [0, y1, z1], // 4: Rear top left
        [t, y1, z1]  // 5: Rear top right
    ];
    
    faces = [
        [0, 2, 3], [0, 3, 1], // Bottom face
        [0, 1, 5], [0, 5, 4], // Sloped face
        [2, 4, 5], [2, 5, 3], // Rear face
        [0, 4, 2],           // Left face
        [1, 3, 5]            // Right face
    ];
    
    polyhedron(points = points, faces = faces);
}

module l_bracket() {
    difference() {
        union() {
            // Base Plate
            translate([-base_x / 2, 0, 0])
                cube([base_x, base_y, base_z]);
            
            // Rear Vertical Plate
            translate([-back_x / 2, base_y - back_y, base_z])
                cube([back_x, back_y, back_z]);
            
            // Left Gusset
            translate([-gusset_x_pos - gusset_thick / 2, 0, 0])
                gusset();
                
            // Right Gusset
            translate([gusset_x_pos - gusset_thick / 2, 0, 0])
                gusset();
        }
        
        // Vertical Mounting Holes in Base
        translate([-base_hole_x_offset, base_hole_y_pos, -1])
            cylinder(d = hole_d, h = base_z + 2);
            
        translate([base_hole_x_offset, base_hole_y_pos, -1])
            cylinder(d = hole_d, h = base_z + 2);
            
        // Horizontal Mounting Holes in Rear Plate
        translate([-back_hole_x_offset, base_y + 1, back_hole_z_pos])
            rotate([90, 0, 0])
                cylinder(d = hole_d, h = back_y + 2);
                
        translate([back_hole_x_offset, base_y + 1, back_hole_z_pos])
            rotate([90, 0, 0])
                cylinder(d = hole_d, h = back_y + 2);
    }
}

// Render Assembly
l_bracket();
