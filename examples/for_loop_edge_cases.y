-- for_loop_edge_cases.y
-- Edge cases for for-in loops: empty/reversed ranges and non-array/
-- non-string iterables (both should run zero iterations, matching
-- the AST interpreter exactly).

fn main() {
    y.println("--- empty range (5..5): should print nothing ---")
    for n in 5..5 {
        y.println(n)
    }
    y.println("(done)")

    y.println("--- reversed range (5..2): should print nothing ---")
    for n in 5..2 {
        y.println(n)
    }
    y.println("(done)")

    y.println("--- empty array: should print nothing ---")
    let empty = []
    for v in empty {
        y.println(v)
    }
    y.println("(done)")

    y.println("--- iterating a non-array/non-string value: zero iterations ---")
    let notIterable = 42
    for v in notIterable {
        y.println(v)
    }
    y.println("(done)")

    y.println("--- nested: outer runs, inner is always empty ---")
    for i in 0..3 {
        y.print(y.format("i={0}: ", i))
        for j in 5..5 {
            y.print("should not print")
        }
        y.println("(inner was empty)")
    }
}
