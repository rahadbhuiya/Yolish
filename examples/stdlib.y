-- stdlib.y
-- Standard library: y.math, y.string, y.array

fn main() {
    y.print("=== y.math ===\n")
    y.println("sqrt(144)      = {y.math.sqrt(144)}")
    y.println("sqrt(256)      = {y.math.sqrt(256)}")
    y.println("pow(2, 10)     = {y.math.pow(2, 10)}")
    y.println("pow(3, 5)      = {y.math.pow(3, 5)}")
    y.println("abs(-99)       = {y.math.abs(-99)}")
    y.println("min(8, 3)      = {y.math.min(8, 3)}")
    y.println("max(8, 3)      = {y.math.max(8, 3)}")
    y.println("clamp(150,0,100)= {y.math.clamp(150, 0, 100)}")
    y.println("sign(-7)       = {y.math.sign(-7)}")
    y.println("sign(0)        = {y.math.sign(0)}")
    y.println("sign(7)        = {y.math.sign(7)}")

    y.print("\n=== y.string ===\n")
    y.println("repeat      : {y.string.repeat(\"ab\", 4)}")
    y.println("starts_with : {y.string.starts_with(\"Exploidus\", \"Exp\")}")
    y.println("ends_with   : {y.string.ends_with(\"Exploidus\", \"dus\")}")
    y.println("replace     : {y.string.replace(\"foo bar foo\", \"foo\", \"baz\")}")
    y.println("reverse     : {y.string.reverse(\"Yolish\")}")
    y.println("pad_left    : [{y.string.pad_left(\"42\", 8)}]")
    y.println("pad_right   : [{y.string.pad_right(\"42\", 8)}]")

    y.print("\n=== y.array ===\n")
    let nums = [5, 2, 8, 1, 9, 3, 7, 4, 6]
    y.println("original    : {nums}")
    y.println("sort        : {y.array.sort(nums)}")
    y.println("reverse     : {y.array.reverse(nums)}")
    y.println("slice(2,6)  : {y.array.slice(nums, 2, 6)}")
    y.println("join        : {y.array.join(nums, \" - \")}")
    y.println("find >7     : {y.array.find(nums, fn(x) { return x > 7 })}")
    y.println("index_of 9  : {y.array.index_of(nums, 9)}")
    y.println("contains 4  : {y.array.contains(nums, 4)}")
    y.println("contains 10 : {y.array.contains(nums, 10)}")

    y.print("\n=== combining stdlib ===\n")
    let names = ["alice", "bob", "charlie", "diana"]
    let upper_names = y.map(names, fn(n) {
        return y.string.repeat(y.upper(y.substr(n, 0, 1)), 1) + y.substr(n, 1, y.len(n) - 1)
    })
    y.println("capitalized : {upper_names}")

    let scores = [88, 45, 92, 61, 77, 55, 98, 33]
    let passing = y.filter(scores, fn(s) { return s >= 60 })
    let sorted  = y.array.sort(passing)
    y.println("passing scores (sorted): {sorted}")

    let total = y.reduce(scores, fn(acc, s) { return acc + s }, 0)
    y.println("average score: {total / y.len(scores)}")
}
