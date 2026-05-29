-- closures.y
-- First-class functions, closures, y.map/filter/reduce

fn apply(f, x) {
    return f(x)
}

fn make_adder(n) {
    return fn(x) { return x + n }
}

fn make_multiplier(n) {
    return fn(x) { return x * n }
}

fn compose(f, g) {
    return fn(x) { return f(g(x)) }
}

fn main() {
    y.print("=== anonymous fn ===\n")
    let double = fn(x) { return x * 2 }
    let square = fn(x) { return x * x }
    y.print("double(5) = ") y.println(double(5))
    y.print("square(4) = ") y.println(square(4))

    y.print("\n=== pass fn as argument ===\n")
    y.print("apply(double, 7) = ") y.println(apply(double, 7))
    y.print("apply(square, 5) = ") y.println(apply(square, 5))

    y.print("\n=== closures ===\n")
    let add5  = make_adder(5)
    let add10 = make_adder(10)
    let triple = make_multiplier(3)
    y.print("add5(3)   = ") y.println(add5(3))
    y.print("add10(3)  = ") y.println(add10(3))
    y.print("triple(7) = ") y.println(triple(7))

    y.print("\n=== compose ===\n")
    let double_then_add5 = compose(add5, double)
    y.print("double_then_add5(4) = ") y.println(double_then_add5(4))

    y.print("\n=== y.map ===\n")
    let nums = [1, 2, 3, 4, 5]
    let doubled = y.map(nums, fn(x) { return x * 2 })
    let squares = y.map(nums, fn(x) { return x * x })
    y.print("doubled : ") y.println(doubled)
    y.print("squares : ") y.println(squares)

    y.print("\n=== y.filter ===\n")
    let evens = y.filter(nums, fn(x) { return x % 2 == 0 })
    let odds  = y.filter(nums, fn(x) { return x % 2 != 0 })
    y.print("evens : ") y.println(evens)
    y.print("odds  : ") y.println(odds)

    y.print("\n=== y.reduce ===\n")
    let sum     = y.reduce(nums, fn(acc, x) { return acc + x }, 0)
    let product = y.reduce(nums, fn(acc, x) { return acc * x }, 1)
    y.print("sum     = ") y.println(sum)
    y.print("product = ") y.println(product)

    y.print("\n=== y.each ===\n")
    y.each(nums, fn(x) { y.print(x) y.print(" ") })
    y.print("\n")
}
