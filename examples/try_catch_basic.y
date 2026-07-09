-- try_catch_basic.y
-- try/catch/throw: catching string and non-string thrown values, catch
-- without binding a variable, and a throw from inside a called function
-- (not just directly inside the try block).

fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

fn safe_get(arr, i) {
    if i < 0 { throw i }
    if i >= y.len(arr) { throw y.format("index {0} out of bounds", i) }
    return arr[i]
}

fn main() {
    try {
        y.println(divide(10, 2))
        y.println(divide(10, 0))
    } catch(e) {
        y.println(y.format("caught: {0}", e))
    }

    let nums = [1, 2, 3]
    try {
        y.println(safe_get(nums, -1))
    } catch(e) {
        y.println(y.format("caught non-string throw: {0}", e))
    }

    try {
        safe_get(nums, 99)
    } catch {
        y.println("caught, don't need the value here")
    }

    y.println("still running after all that")
}
