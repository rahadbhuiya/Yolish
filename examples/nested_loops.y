-- nested_loops.y
-- Nested loops (while-in-while, for-in-for, mixed) — exercises the
-- per-iteration local-variable cleanup fix. Before that fix, a fresh
-- `let`/loop-var declared inside an outer loop's body would corrupt
-- its stack slot on the outer loop's 2nd+ iteration.

fn main() {
    y.println("--- nested while: break only breaks inner loop ---")
    var i = 0
    while i < 3 {
        let label = i * 10
        var j = 0
        while j < 5 {
            j = j + 1
            if j == 4 {
                break
            }
            y.print(label + j) y.print(" ")
        }
        y.print("| ")
        i = i + 1
    }
    y.print("\n")

    y.println("--- nested for-range: break only breaks inner loop ---")
    for a in 0..3 {
        for b in 0..5 {
            if b == 3 {
                break
            }
            y.print(y.format("{0}", b)) y.print(" ")
        }
        y.print("| ")
    }
    y.print("\n")

    y.println("--- three levels deep, with a `let` at each level ---")
    for x in 0..3 {
        for y2 in 0..3 {
            let prod = x * y2
            for z in 0..2 {
                if z == 1 {
                    continue
                }
                y.print(y.format("({0},{1},{2})={3} ", x, y2, z, prod))
            }
        }
    }
    y.print("\n")

    y.println("--- for-array of for-string (mixed nesting) ---")
    let words = ["a", "bb", "ccc"]
    for w in words {
        var cnt = 0
        for ch in w {
            cnt = cnt + 1
        }
        y.println(y.format("{0} -> len {1}", w, cnt))
    }
}
