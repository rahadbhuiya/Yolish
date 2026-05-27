-- structs.y
-- Structs: define, instantiate, field access

struct Point {
    x, y
}

struct Rect {
    x, y, width, height
}

struct Person {
    name, age
}

fn make_point(px, py) {
    return Point { x: px, y: py }
}

fn area(r) {
    return r.width * r.height
}

fn greet(p) {
    y.println(y.format("Hello, {0}! You are {1} years old.", p.name, p.age))
}

fn main() {
    let p = Point { x: 10, y: 20 }
    y.print("Point x : ") y.println(p.x)
    y.print("Point y : ") y.println(p.y)

    let p2 = make_point(5, 15)
    y.print("p2.x    : ") y.println(p2.x)

    let r = Rect { x: 0, y: 0, width: 100, height: 50 }
    y.print("Area    : ") y.println(area(r))

    let person = Person { name: "Diaz", age: 22 }
    greet(person)
}
