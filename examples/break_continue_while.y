-- break_continue_while.y
-- Basic break and continue inside a plain while loop

fn main() {
    y.println("--- break: stop at 5 ---")
    var i = 0
    while i < 10 {
        i = i + 1
        if i == 5 {
            break
        }
        y.print(i) y.print(" ")
    }
    y.print("\n")

    y.println("--- continue: skip evens ---")
    var j = 0
    var sum = 0
    while j < 10 {
        j = j + 1
        if j % 2 == 0 {
            continue
        }
        sum = sum + j
        y.print(j) y.print(" ")
    }
    y.print("\n")
    y.println(y.format("sum of odds: {0}", sum))
}
