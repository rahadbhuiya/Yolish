-- error_objects.y
-- Error objects with message and code fields

fn divide(a, b) {
    if b == 0 {
        throw y.error("division by zero", 400)
    }
    return a / b
}

fn fetch(url, allowed) {
    if !y.array.contains(allowed, url) {
        throw y.error("access denied: {url}", 403)
    }
    return "data from {url}"
}

fn describe_code(code) {
    if code == 200 { return "OK" }
    if code == 400 { return "Bad Request" }
    if code == 401 { return "Unauthorized" }
    if code == 403 { return "Forbidden" }
    if code == 404 { return "Not Found" }
    if code == 500 { return "Internal Server Error" }
    return "Unknown"
}

fn main() {
    y.print("=== basic error object ===\n")
    let err = y.error("not found", 404)
    y.println("message : {err.message}")
    y.println("code    : {err.code}")

    y.print("\n=== throw and catch error object ===\n")
    try {
        throw y.error("unauthorized", 401)
    } catch(e) {
        y.println("caught  : {e.message} (code {e.code})")
    }

    y.print("\n=== error from function ===\n")
    try {
        y.println(divide(10, 2))
        y.println(divide(10, 0))
    } catch(e) {
        y.println("error   : {e.message} (code {e.code})")
    }

    y.print("\n=== access control ===\n")
    let allowed = ["/home", "/docs", "/api"]
    let urls    = ["/home", "/secret", "/docs", "/admin"]
    for url in urls {
        try {
            let data = fetch(url, allowed)
            y.println("  ok    : {data}")
        } catch(e) {
            y.println("  error : {e.message} (code {e.code})")
        }
    }

    y.print("\n=== error codes ===\n")
    let codes = [200, 400, 401, 403, 404, 500]
    for code in codes {
        y.println("  {code} {describe_code(code)}")
    }
}
