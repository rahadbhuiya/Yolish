-- path_demo.y  —  v1.7: Path

let full = "/home/bhuiya/projects/hello.y"

y.print("basename : ") y.println(y.path.basename(full))
y.print("dirname  : ") y.println(y.path.dirname(full))
y.print("ext      : ") y.println(y.path.ext(full))
y.print("stem     : ") y.println(y.path.stem(full))
y.print("abs      : ") y.println("(platform-dependent)")
y.print("joined   : ") y.println(y.path.join("/home", "bhuiya", "code"))

let ext = y.path.ext("script.y")
if ext == ".y" {
    y.println("This is a Yolish file!")
}
