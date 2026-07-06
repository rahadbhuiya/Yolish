-- toplevel_loops.y
-- break/continue and for-in loops used at TOP LEVEL (global scope,
-- outside any fn). This exercises the OP_DEFINE_GLOBAL/OP_GET_GLOBAL/
-- OP_SET_GLOBAL path instead of locals — a different code path from
-- every other example here, which all run inside fn main().

var total = 0
for n in 1..10 {
    if n == 7 {
        break
    }
    if n % 2 == 0 {
        continue
    }
    total = total + n
}
y.println(y.format("total: {0}", total))

var k = 0
while k < 5 {
    k = k + 1
    if k == 3 {
        continue
    }
    y.print(k) y.print(" ")
}
y.print("\n")
