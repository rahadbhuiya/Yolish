-- try_catch_nested.y
-- Nested try/catch with rethrow, a throw from inside a for-loop caught
-- by an enclosing try, and a throw propagating up through several
-- levels of function calls before being caught.

fn main() {
    y.println("--- nested try/catch with rethrow ---")
    try {
        try {
            throw "inner problem"
        } catch(e) {
            y.println(y.format("inner caught: {0}", e))
            throw y.format("wrapped: {0}", e)
        }
    } catch(e) {
        y.println(y.format("outer caught: {0}", e))
    }

    y.println("--- throw inside a loop ---")
    try {
        for i in 0..10 {
            if i == 4 { throw y.format("stopped at {0}", i) }
            y.println(i)
        }
    } catch(e) {
        y.println(y.format("loop caught: {0}", e))
    }

    y.println("--- throw from a deep call chain ---")
    fn level3() { throw "deep error" }
    fn level2() { return level3() }
    fn level1() { return level2() }
    try {
        level1()
    } catch(e) {
        y.println(y.format("deep caught: {0}", e))
    }

    y.println("all done, execution continued normally")
}
