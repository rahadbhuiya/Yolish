-- mathlib.y
-- A reusable math library — import this in other files

fn square(n) {
    return n * n
}

fn cube(n) {
    return n * n * n
}

fn max(a, b) {
    if a > b { return a }
    return b
}

fn min(a, b) {
    if a < b { return a }
    return b
}

fn abs_val(n) {
    if n < 0 { return n * -1 }
    return n
}

fn clamp(val, lo, hi) {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
}
