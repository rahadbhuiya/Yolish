-- time_demo.y  —  v1.7: Time

let now = y.time.now()
y.println(y.time.format(now, "%Y-%m-%d %H:%M:%S"))
y.println(y.time.format(now, "%Y-%m-%d"))
y.println(y.time.unix())

y.println("Sleeping 300ms...")
y.time.sleep(300)
let after = y.time.now()
y.print("Elapsed ms: ")
y.println(after - now)
