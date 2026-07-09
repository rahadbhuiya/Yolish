-- closures_basic.y
-- Anonymous functions, closures capturing an enclosing parameter, and
-- passing closures to higher-order builtins (y.map/filter/reduce).

fn apply(f, x) {
    return f(x)
}

fn make_adder(n) {
    return fn(x) { return x + n }
}

fn compose(f, g) {
    return fn(x) { return f(g(x)) }
}

fn main() {
    let double = fn(x) { return x * 2 }
    y.println(double(5))
    y.println(apply(double, 7))

    let add5 = make_adder(5)
    let add10 = make_adder(10)
    y.println(add5(3))
    y.println(add10(3))

    let double_then_add5 = compose(add5, double)
    y.println(double_then_add5(4))

    let nums = [1, 2, 3, 4, 5]
    y.println(y.map(nums, fn(x) { return x * x }))
    y.println(y.filter(nums, fn(x) { return x % 2 == 0 }))
    y.println(y.reduce(nums, fn(acc, x) { return acc + x }, 0))
}
