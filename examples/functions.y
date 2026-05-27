-- functions.y
-- Functions, recursion, return values

fn add(a, b) {
    return a + b
}

fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

fn is_even(n) {
    if n % 2 == 0 { return true }
    return false
}

fn main() {
    y.print("3 + 4     = ") y.println(add(3, 4))
    y.print("10 + 20   = ") y.println(add(10, 20))
    y.print("5!        = ") y.println(factorial(5))
    y.print("10!       = ") y.println(factorial(10))
    y.print("4 is even : ") y.println(is_even(4))
    y.print("7 is even : ") y.println(is_even(7))
}
