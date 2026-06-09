-- float_math.y  —  v1.1: Float arithmetic

let pi = 3.14159
let r  = 5.0

let area      = pi * r * r
let perimeter = 2.0 * pi * r

y.println("Area:")
y.println(area)

y.println("Perimeter:")
y.println(perimeter)

-- Basic arithmetic
let a = 1.5
let b = 2.7
y.println(a + b)
y.println(a < b)

-- Negative float
let temp = -3.14
y.println(temp)

-- Int + float mixing
let x = 10
let y2 = x + 0.5
y.println(y2)

-- Float comparison
let q = 10.0 / 4.0
y.println(q)
y.println(q == 2.5)
