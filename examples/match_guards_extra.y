-- match_guards_extra.y
-- Match guards (pattern binding + `if` condition), nested match, and
-- a binding that shadows an outer variable of the same name.

fn classify(n) {
    return match n {
        0            => "zero"
        n if n < 0   => "negative"
        n if n < 10  => "small positive"
        n if n < 100 => "medium"
        _            => "large"
    }
}

fn main() {
    for n in [0, -42, 7, 55, 999] {
        y.println(y.format("{0} -> {1}", n, classify(n)))
    }

    y.println("--- nested match ---")
    let x = 5
    let yv = 10
    let result = match x {
        n if n > 0 => match yv {
            m if m > n => "yv bigger"
            _          => "x bigger or equal"
        }
        _ => "x not positive"
    }
    y.println(result)

    y.println("--- binding shadows an outer variable of the same name ---")
    let n = 100
    let shadowed = match 7 {
        n => n * 2
    }
    y.println(y.format("shadowed result: {0}, outer n still: {1}", shadowed, n))

    y.println("--- compound guard (&&) ---")
    fn season(m) {
        return match m {
            m if m >= 3 && m <= 5  => "Spring"
            m if m >= 6 && m <= 8  => "Summer"
            m if m >= 9 && m <= 11 => "Autumn"
            _                      => "Winter"
        }
    }
    for m in y.range(1, 13) {
        y.println(y.format("month {0}: {1}", m, season(m)))
    }
}
