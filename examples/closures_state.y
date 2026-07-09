-- closures_state.y
-- The hardest closure pattern to get right: a captured variable that
-- gets *mutated* across multiple calls to the *same* closure, with
-- independent state per closure instance. Also: capturing more than
-- one variable, and a closure that returns another closure built from
-- captured functions.

fn make_counter() {
    var count = 0
    return fn() {
        count = count + 1
        return count
    }
}

fn make_calc(a, b) {
    return fn(op) {
        if op == "add" { return a + b }
        if op == "sub" { return a - b }
        return a * b
    }
}

fn twice(f) {
    return fn(x) { return f(f(x)) }
}

fn main() {
    y.println("--- independent counters ---")
    let c1 = make_counter()
    let c2 = make_counter()
    y.println(c1())
    y.println(c1())
    y.println(c1())
    y.println(c2())
    y.println(c1())
    y.println(c2())

    y.println("--- multi-variable capture ---")
    let calc = make_calc(10, 3)
    y.println(calc("add"))
    y.println(calc("sub"))
    y.println(calc("mul"))

    y.println("--- closure built from a captured closure ---")
    let inc = fn(x) { return x + 1 }
    let inc2 = twice(inc)
    y.println(inc2(5))

    y.println("--- custom sort comparator (descending) ---")
    y.println(y.sort([5, 2, 8, 1, 9], fn(a, b) { return a > b }))
}
