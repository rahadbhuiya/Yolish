-- enums_basic.y
-- Enum declaration, variant access, matching on enum values, equality
-- comparison, and passing enum values around (arrays, loops, function
-- arguments) — multiple enums coexisting in the same program.

enum Color { Red Green Blue }
enum Size { Small Medium Large }

fn describe(c) {
    match c {
        Color.Red   => "warm"
        Color.Green => "natural"
        Color.Blue  => "cool"
    }
}

fn main() {
    y.println(Color.Red)
    y.println(Color.Green)

    let c = Color.Blue
    y.println(describe(c))
    y.println(describe(Color.Red))

    let colors = [Color.Red, Color.Green, Color.Blue]
    for col in colors {
        y.println(describe(col))
    }

    y.println(Color.Red == Color.Red)
    y.println(Color.Red == Color.Blue)

    y.println(Size.Small)
    y.println(Size.Medium)
    y.println(Size.Large)
}
