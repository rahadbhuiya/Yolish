-- interpolation.y
-- String interpolation: embed expressions inside strings with {expr}

struct Person {
    name, age, city
}

fn main() {
    y.print("=== basic interpolation ===\n")
    let name = "Diaz"
    let age  = 22
    let lang = "Yolish"
    y.println("Hello, {name}!")
    y.println("You are {age} years old.")
    y.println("Next year: {age + 1}")
    y.println("Uppercase: {y.upper(name)}")
    y.println("Language: {lang} v0.6")

    y.print("\n=== expressions in strings ===\n")
    let x = 6
    let y2 = 7
    y.println("{x} * {y2} = {x * y2}")
    y.println("Is even: {x % 2 == 0}")
    y.println("Max: {y.math.max(x, y2)}")

    y.print("\n=== struct fields ===\n")
    let p = Person { name: "Diaz", age: 22, city: "Dhaka" }
    y.println("Hello, {p.name}! Age {p.age} in {p.city}.")

    y.print("\n=== in loops ===\n")
    for i in 1..6 {
        if i % 3 == 0 {
            y.println("  {i}: Fizz")
        } else if i % 5 == 0 {
            y.println("  {i}: Buzz")
        } else {
            y.println("  {i}: {i}")
        }
    }

    y.print("\n=== array info ===\n")
    let arr = [10, 20, 30, 40, 50]
    y.println("Array: {arr}")
    y.println("Length: {y.len(arr)}, First: {arr[0]}, Last: {arr[4]}")
}
