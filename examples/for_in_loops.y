-- for_in_loops.y
-- for-in loops: range form (a..b) and array/string iteration form,
-- both with break and continue

fn main() {
    y.println("--- for range break ---")
    for n in 0..20 {
        if n * n > 50 {
            break
        }
        y.print(n) y.print(" ")
    }
    y.print("\n")

    y.println("--- for range continue: skip multiples of 3 ---")
    for n in 1..16 {
        if n % 3 == 0 {
            continue
        }
        y.print(n) y.print(" ")
    }
    y.print("\n")

    y.println("--- for array break ---")
    let words = ["apple", "banana", "STOP", "cherry", "date"]
    for w in words {
        if w == "STOP" {
            break
        }
        y.println(w)
    }

    y.println("--- for array continue: skip negatives ---")
    let vals = [3, -1, 7, -4, 2, -9, 5]
    var sum = 0
    for v in vals {
        if v < 0 {
            continue
        }
        sum = sum + v
    }
    y.println(y.format("sum of positives: {0}", sum))

    y.println("--- for string: count vowels ---")
    let word = "programming"
    var vowels = 0
    for ch in word {
        if ch == "a" { vowels = vowels + 1 }
        if ch == "e" { vowels = vowels + 1 }
        if ch == "i" { vowels = vowels + 1 }
        if ch == "o" { vowels = vowels + 1 }
        if ch == "u" { vowels = vowels + 1 }
    }
    y.println(y.format("vowels in '{0}': {1}", word, vowels))
}
