-- types.y
-- y.typeof and type check predicates

fn describe(v) {
    let t = y.typeof(v)
    match t {
        "int"    => { y.print("integer: ") y.println(v) }
        "float"  => { y.print("float  : ") y.println(v) }
        "str"    => { y.print("string : ") y.println(v) }
        "bool"   => { y.print("bool   : ") y.println(v) }
        "array"  => { y.print("array  : ") y.println(v) }
        "fn"     => { y.print("function (anonymous)\n") }
        "nil"    => { y.print("nil\n") }
        _        => { y.print("other  : ") y.println(t) }
    }
}

fn safe_double(v) {
    if y.is_int(v) {
        return v * 2
    }
    throw y.format("expected int, got {0}", y.typeof(v))
}

fn main() {
    y.print("=== y.typeof ===\n")
    describe(42)
    describe(3.14)
    describe("Yolish")
    describe(true)
    describe([1, 2, 3])
    describe(fn(x) { return x })

    y.print("\n=== type predicates ===\n")
    let vals = [10, "hello", true, 3.14, [1,2]]
    for v in vals {
        y.print(y.typeof(v))
        y.print(" -> is_int=") y.print(y.is_int(v))
        y.print(" is_str=")   y.println(y.is_str(v))
    }

    y.print("\n=== type-safe function ===\n")
    try {
        y.println(safe_double(5))
        y.println(safe_double("oops"))
    } catch(e) {
        y.print("type error: ") y.println(e)
    }
}
