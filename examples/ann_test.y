-- ann_test.y
-- @intent and @audit annotation examples

@intent("compute")
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

@intent("compute")
fn fibonacci(n) {
    if n <= 1 { return n }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

@audit("sensitive")
fn get_secret() {
    return "top-secret-key-123"
}

@audit("auth")
fn login(user, pass) {
    if user == "admin" {
        if pass == "1234" { return true }
    }
    return false
}

@audit("write")
fn save_log(msg) {
    let f = cap.open("/tmp/yolish_audit.log", 2)
    cap.write(f, msg)
    cap.write(f, "\n")
    cap.close(f)
}

fn main() {
    y.print("fib(8)     = ") y.println(fibonacci(8))
    y.print("10!        = ") y.println(factorial(10))

    let secret = get_secret()
    y.print("secret     = ") y.println(secret)

    save_log("startup event")
    save_log("user connected")

    y.print("login ok   = ") y.println(login("admin", "1234"))
    y.print("login fail = ") y.println(login("admin", "wrong"))
}
