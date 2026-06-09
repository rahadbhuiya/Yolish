-- utils.y  —  imported by module_demo.y

fn greet(name) {
    y.print("Hello, ")
    y.print(name)
    y.println("!")
}

fn add(a, b) {
    return a + b
}

fn max_val(a, b) {
    if a > b { return a }
    return b
}

let UTILS_VERSION = "1.0"
