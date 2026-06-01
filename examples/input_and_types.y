-- input_and_types.y
-- y.input, y.input_int, y.input_float
-- y.int, y.float, y.bool, y.str type conversions

fn main() {
    y.println("=== type conversions ===")

    -- y.str: anything to string
    y.println(y.str(42))          -- "42"
    y.println(y.str(-3.14))       -- "-3.14"
    y.println(y.str(true))        -- "true"
    y.println(y.str([1, 2, 3]))   -- "[1, 2, 3]"

    -- y.int: string or float to int
    y.println(y.int("42"))        -- 42
    y.println(y.int("-17"))       -- -17
    y.println(y.int(3.9))         -- 3  (truncates)

    -- y.float: string or int to float
    y.println(y.float("3.14"))    -- 3.14
    y.println(y.float("-0.5"))    -- -0.5
    y.println(y.float(42))        -- 42.0

    -- y.bool: string or int to bool
    y.println(y.bool("true"))     -- true
    y.println(y.bool("false"))    -- false
    y.println(y.bool("1"))        -- true
    y.println(y.bool(0))          -- false

    -- float arithmetic
    let a = y.float("1.5")
    let b = y.float("2.25")
    y.println(y.str(a + b))       -- 3.75
    y.println(y.str(a * b))       -- 3.375

    y.println("=== interactive input ===")

    let name = y.input("Your name: ")
    y.println(y.format(r"Hello, {0}!", name))

    let age = y.input_int("Your age: ")
    y.println(y.format(r"In 10 years you will be {0}.", age + 10))

    let weight = y.input_float("Weight in kg: ")
    let bmi_w  = y.input_float("Height in m:  ")
    let bmi    = weight / (bmi_w * bmi_w)
    y.println(y.format(r"Your BMI is approx. {0}", y.str(bmi)))
}
