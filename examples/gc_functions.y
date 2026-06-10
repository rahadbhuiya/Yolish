-- gc_functions.y  —  v1.5: GC with function return values

y.println("=== Function Return Test ===")

-- Function that creates and returns an array
fn make_nums(size) {
    let result = [10, 20, 30, 40, 50]
    return result
}

-- Function that creates a temporary array internally (not returned)
fn process_data() {
    let temp = [1, 2, 3, 4, 5, 6, 7, 8]
    let total = y.sum(temp)
    return total   -- only the int is returned, temp array is reclaimable
}

-- Call functions and check GC
let b1 = gc.stats()

let nums = make_nums(5)
let sum1 = process_data()
let sum2 = process_data()
let sum3 = process_data()

gc.collect()
let b2 = gc.stats()

y.println("")
y.print("nums  : ") y.println(nums)
y.print("sum   : ") y.println(sum1)
y.println("")
y.print("alloc : ") y.println(b2.alloc)
y.print("freed : ") y.println(b2.freed)
y.print("live  : ") y.println(b2.live)

y.println("")
y.println("=== Nested Function Test ===")

fn inner() {
    return [100, 200, 300]
}

fn outer() {
    let x = inner()
    let y2 = inner()
    return y2
}

let result = outer()
y.println(result)

gc.collect()
let b3 = gc.stats()
y.print("live after nested: ") y.println(b3.live)
