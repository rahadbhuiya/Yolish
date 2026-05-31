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

fn is_prime(n) {
    if n < 2 { return false }
    var i = 2
    while i * i <= n {
        if n % i == 0 { return false }
        i = i + 1
    }
    return true
}
fn factorial(n) {
    var result = 1
    var i = 1
    while i <= n {
        result = result * i
        i = i + 1
    }
    return result
}
fn fib(n) {
    if n <= 1 { return n }
    var a = 0
    var b = 1
    var i = 2
    while i <= n {
        var c = a + b
        a = b
        b = c
        i = i + 1
    }
    return b
}
