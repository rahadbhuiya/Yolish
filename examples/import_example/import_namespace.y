-- import_namespace.y
-- import "file.y" as name: the whole file's definitions become a
-- namespace struct, accessed via name.thing. Needs
-- import_ns_mathutils.y in the same directory.

import "./import_ns_mathutils.y" as math

fn main() {
    y.println(math.square(5))
    y.println(math.cube(3))
    y.println(math.PI)

    -- a module function calling another module function/constant
    -- internally (base() and FACTOR are only visible inside the module)
    y.println(math.base(5))
    y.println(math.combo(5))
}
