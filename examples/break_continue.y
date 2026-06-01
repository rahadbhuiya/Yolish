-- break_continue.y
-- break and continue inside while, for-range, and for-array loops

fn main() {
    y.println("--- break: stop at 5 ---")
    var i = 0
    while i < 10 {
        if i == 5 { break }
        y.print(i) y.print(" ")
        i = i + 1
    }
    y.print("\n")

    y.println("--- continue: skip evens ---")
    var j = 0
    while j < 8 {
        j = j + 1
        if j % 2 == 0 { continue }
        y.print(j) y.print(" ")
    }
    y.print("\n")

    y.println("--- for range break ---")
    for n in 0..20 {
        if n * n > 50 { break }
        y.print(n) y.print(" ")
    }
    y.print("\n")

    y.println("--- for range continue: skip multiples of 3 ---")
    for n in 1..16 {
        if n % 3 == 0 { continue }
        y.print(n) y.print(" ")
    }
    y.print("\n")

    y.println("--- for array break ---")
    let words = ["apple", "banana", "STOP", "cherry", "date"]
    for w in words {
        if w == "STOP" { break }
        y.println(w)
    }

    y.println("--- for array continue: skip negatives ---")
    let vals = [3, -1, 7, -4, 2, -9, 5]
    var sum = 0
    for v in vals {
        if v < 0 { continue }
        sum = sum + v
    }
    y.println(y.format("sum of positives: {0}", sum))

    y.println("--- nested: break only breaks inner loop ---")
    for i in 0..3 {
        for j in 0..5 {
            if j == 3 { break }
            y.print(y.format("{0}", j)) y.print(" ")
        }
        y.print("| ")
    }
    y.print("\n")

    y.println("--- break/continue do not escape functions ---")
    fn find_first(arr, target) {
        var idx = 0
        for x in arr {
            if x == target { return idx }
            idx = idx + 1
        }
        return -1
    }
    y.println(y.format("index of 7: {0}", find_first([2, 5, 7, 3, 9], 7)))
    y.println(y.format("index of 4: {0}", find_first([2, 5, 7, 3, 9], 4)))
}
