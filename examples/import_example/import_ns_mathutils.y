-- import_ns_mathutils.y  —  imported by import_namespace.y with "as"

let FACTOR = 10

fn square(x) { return x * x }
fn cube(x) { return x * x * x }

fn base(x) { return x * FACTOR }
fn combo(x) { return base(x) + base(x + 1) }

let PI = 3.14159
