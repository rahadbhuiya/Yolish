-- match_guards.y
-- Match guards (if condition) and pattern binding

fn main() {
    y.println("=== guard: classify number ===")
    fn classify(n) {
        return match n {
            0            => "zero"
            n if n < 0   => "negative"
            n if n < 10  => "small positive"
            n if n < 100 => "medium"
            _            => "large"
        }
    }
    y.println(classify(0))
    y.println(classify(-42))
    y.println(classify(7))
    y.println(classify(55))
    y.println(classify(999))

    y.println("=== guard: grade score ===")
    fn grade(s) {
        return match s {
            s if s >= 90 => "A"
            s if s >= 80 => "B"
            s if s >= 70 => "C"
            s if s >= 60 => "D"
            _            => "F"
        }
    }
    y.println(grade(95))
    y.println(grade(83))
    y.println(grade(71))
    y.println(grade(45))

    y.println("=== guard: string length ===")
    fn label(s) {
        return match s {
            s if y.len(s) == 0   => "(empty)"
            s if y.len(s) <= 5   => y.format(r"short: '{0}'", s)
            s if y.len(s) <= 15  => y.format(r"medium: '{0}'", s)
            s                    => y.format(r"long ({0} chars)", y.len(s))
        }
    }
    y.println(label(""))
    y.println(label("hi"))
    y.println(label("hello world"))
    y.println(label("this is a very long string indeed"))

    y.println("=== fizzbuzz with guards ===")
    fn fizzbuzz(n) {
        return match n {
            n if n % 15 == 0 => "FizzBuzz"
            n if n % 3  == 0 => "Fizz"
            n if n % 5  == 0 => "Buzz"
            n                => y.str(n)
        }
    }
    for i in y.range(1, 21) {
        y.print(fizzbuzz(i)) y.print(" ")
    }
    y.print("\n")

    y.println("=== season with compound guard ===")
    fn season(m) {
        return match m {
            m if m >= 3 && m <= 5  => "Spring"
            m if m >= 6 && m <= 8  => "Summer"
            m if m >= 9 && m <= 11 => "Autumn"
            _                      => "Winter"
        }
    }
    let months = ["Jan","Feb","Mar","Apr","May","Jun",
                  "Jul","Aug","Sep","Oct","Nov","Dec"]
    for i in y.range(12) {
        y.println(y.format(r"{0}: {1}", months[i], season(i + 1)))
    }
}
