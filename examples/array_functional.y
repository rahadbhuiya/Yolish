-- array_functional.y
-- y.range, y.sort, y.zip, y.map, y.filter, y.reduce, y.sum, y.flatten, y.each

fn main() {
    y.println("=== y.range ===")
    y.println(y.range(5))               -- [0, 1, 2, 3, 4]
    y.println(y.range(2, 7))            -- [2, 3, 4, 5, 6]
    y.println(y.range(0, 10, 2))        -- [0, 2, 4, 6, 8]
    y.println(y.range(10, 0, -3))       -- [10, 7, 4, 1]

    y.println("=== y.sort ===")
    y.println(y.sort([5, 2, 8, 1, 9, 3]))
    y.println(y.sort(["banana", "apple", "cherry", "date"]))

    y.println("--- descending (custom comparator) ---")
    y.println(y.sort([5, 2, 8, 1, 9, 3], fn(a, b) { return a > b }))

    y.println("=== y.map ===")
    let nums = [1, 2, 3, 4, 5]
    y.println(y.map(nums, fn(x) { return x * x }))
    y.println(y.map(["hello", "world"], fn(s) { return y.upper(s) }))

    y.println("=== y.filter ===")
    y.println(y.filter(y.range(1, 16), fn(n) { return n % 3 == 0 }))
    y.println(y.filter(["", "hi", "", "there", ""], fn(s) { return y.len(s) > 0 }))

    y.println("=== y.reduce ===")
    let sum = y.reduce(y.range(1, 6), fn(acc, x) { return acc + x }, 0)
    y.println(sum)
    let max = y.reduce([3, 7, 2, 9, 4], fn(m, x) { if x > m { return x } return m }, 0)
    y.println(max)

    y.println("=== y.sum ===")
    y.println(y.sum([1, 2, 3, 4, 5]))
    y.println(y.sum(y.range(1, 101)))

    y.println("=== y.zip ===")
    let names  = ["Diaz", "Rizz", "Oro"]
    let scores = [95, 87, 72]
    let pairs  = y.zip(names, scores)
    for p in pairs {
        y.println(y.format(r"  {0}: {1}", p.first, p.second))
    }

    y.println("=== y.flatten ===")
    y.println(y.flatten([[1, 2], [3, 4], [5, 6]]))
    y.println(y.flatten([[10], [], [20, 30]]))

    y.println("=== y.each (side effects) ===")
    y.each([1, 2, 3], fn(x) { y.print(y.format(r"{0} ", x)) })
    y.print("\n")

    y.println("=== chained pipeline ===")
    -- sum of squares of odd numbers 1..10
    let result = y.reduce(
        y.map(
            y.filter(y.range(1, 11), fn(x) { return x % 2 == 1 }),
            fn(x) { return x * x }
        ),
        fn(acc, x) { return acc + x },
        0
    )
    y.println(result)     -- 1+9+25+49+81 = 165

    y.println("=== sort + zip + map ===")
    let raw = [42, 7, 23, 15, 3]
    let ranked = y.zip(y.range(1, y.len(raw) + 1), y.sort(raw))
    for r in ranked {
        y.println(y.format(r"  rank {0}: {1}", r.first, r.second))
    }
}
