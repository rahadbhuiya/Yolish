-- let_inside_loop.y
-- A plain `let` declared fresh inside a while-loop body, once per
-- iteration. Before the local-scope cleanup fix, the VM would print
-- "0 0 0" here (every iteration reading iteration 1's stale value)
-- instead of the correct "0 10 20".

fn main() {
    var i = 0
    while i < 3 {
        let x = i * 10
        y.println(x)
        i = i + 1
    }
}
