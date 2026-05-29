-- errors.y
-- try/catch/throw error handling

fn divide(a, b) {
    if b == 0 {
        throw "division by zero"
    }
    return a / b
}

fn safe_get(arr, i) {
    if i < 0 {
        throw "index out of bounds: negative"
    }
    if i >= y.len(arr) {
        throw "index out of bounds: too large"
    }
    return arr[i]
}

fn parse_positive(s) {
    let n = y.int(s)
    if n < 0 {
        throw "expected positive number"
    }
    return n
}

fn main() {
    y.print("=== basic try/catch/throw ===\n")
    try {
        y.println(divide(10, 2))
        y.println(divide(10, 0))
    } catch(e) {
        y.print("caught: ") y.println(e)
    }

    y.print("\n=== catch per call ===\n")
    let nums = [10, 20, 30]
    for i in 0..5 {
        try {
            let val = safe_get(nums, i)
            y.print("nums[") y.print(i) y.print("] = ") y.println(val)
        } catch(e) {
            y.print("error at ") y.print(i) y.print(": ") y.println(e)
        }
    }

    y.print("\n=== throw from nested fn ===\n")
    try {
        let a = divide(100, 5)
        let b = divide(a, 0)
        y.println(b)
    } catch(e) {
        y.print("caught deep: ") y.println(e)
    }

    y.print("\n=== continue after catch ===\n")
    var i = 0
    while i < 3 {
        try {
            if i == 1 { throw "skip one" }
            y.print("step ") y.println(i)
        } catch(e) {
            y.print("skipped: ") y.println(e)
        }
        i = i + 1
    }
}
