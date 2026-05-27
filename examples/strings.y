-- strings.y
-- String builtins and y.format

fn main() {
    let s = "  Hello, Yolish!  "

    y.print("original : '") y.print(s)            y.println("'")
    y.print("trim     : '") y.print(y.trim(s))    y.println("'")
    y.print("upper    :  ") y.println(y.upper(y.trim(s)))
    y.print("lower    :  ") y.println(y.lower(y.trim(s)))
    y.print("substr   :  ") y.println(y.substr("Exploidus OS", 0, 8))
    y.print("contains :  ") y.println(y.contains("Exploidus OS", "OS"))
    y.print("len      :  ") y.println(y.len("Yolish"))

    y.println("--- split ---")
    let parts = y.split("one,two,three,four", ",")
    for p in parts {
        y.println(p)
    }

    y.println("--- format ---")
    let msg = y.format("OS: {0}  Lang: {1}  Version: {2}", "Exploidus", "Yolish", 3)
    y.println(msg)

    y.println("--- char iteration ---")
    for ch in "Yolish" {
        y.print(ch) y.print(" ")
    }
    y.print("\n")
}
