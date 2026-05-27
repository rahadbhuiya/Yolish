-- main.y
-- Demonstrates import: uses mathlib.y

import "mathlib.y"

fn main() {
    y.println(y.format("square(7)        = {0}", square(7)))
    y.println(y.format("cube(4)          = {0}", cube(4)))
    y.println(y.format("max(10, 25)      = {0}", max(10, 25)))
    y.println(y.format("min(10, 25)      = {0}", min(10, 25)))
    y.println(y.format("abs_val(-42)     = {0}", abs_val(-42)))
    y.println(y.format("clamp(150, 0,100)= {0}", clamp(150, 0, 100)))
}
