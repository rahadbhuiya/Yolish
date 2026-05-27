-- loops.y
-- while, for range, for array, for string, nested loops

fn main() {
    y.println("--- while ---")
    var i = 0
    while i < 5 {
        y.print(i) y.print(" ")
        i = i + 1
    }
    y.print("\n")

    y.println("--- for range ---")
    for n in 1..6 {
        y.print(n) y.print(" ")
    }
    y.print("\n")

    y.println("--- for array ---")
    let days = ["Mon", "Tue", "Wed", "Thu", "Fri"]
    for d in days {
        y.print(d) y.print(" ")
    }
    y.print("\n")

    y.println("--- multiplication table (3) ---")
    for n in 1..11 {
        y.println(y.format("3 x {0} = {1}", n, 3 * n))
    }

    y.println("--- fizzbuzz 1..20 ---")
    for n in 1..21 {
        match n % 15 {
            0 => y.println("FizzBuzz")
            _ => {
                match n % 3 {
                    0 => y.println("Fizz")
                    _ => {
                        match n % 5 {
                            0 => y.println("Buzz")
                            _ => y.println(n)
                        }
                    }
                }
            }
        }
    }
}
