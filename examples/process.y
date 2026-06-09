-- process.y  —  v1.3: Process & System

let kernel = process.spawn("uname -s")
y.print("Kernel: ")
y.print(kernel)

let home = process.env("HOME")
y.print("Home: ")
y.println(home)

y.print("Platform: ")
y.println(sys.platform())

y.print("PID: ")
y.println(process.pid())

let code = process.spawn_code("true")
y.println(code)

y.env.set("YS_VERSION", "1.1")
let ver = y.env.get("YS_VERSION")
y.print("YS_VERSION = ")
y.println(ver)
