-- match_basic.y
-- match expressions: literal patterns, range patterns, wildcard, and
-- match as a function's implicit return value (no explicit `return`)

fn grade(score) {
    match score {
        100     => "Perfect"
        90..100 => "A"
        80..90  => "B"
        70..80  => "C"
        60..70  => "D"
        _       => "F"
    }
}

fn main() {
    let scores = [100, 95, 85, 72, 61, 40]
    for s in scores {
        y.println(y.format("{0} => {1}", s, grade(s)))
    }

    y.println("--- match as a plain statement (result discarded) ---")
    let day = 3
    match day {
        1 => y.println("Monday")
        2 => y.println("Tuesday")
        3 => y.println("Wednesday")
        _ => y.println("some other day")
    }

    y.println("--- match with a bound variable, no guard ---")
    let anyNum = 17
    let described = match anyNum {
        0 => "zero"
        n => y.format("non-zero: {0}", n)
    }
    y.println(described)
}
