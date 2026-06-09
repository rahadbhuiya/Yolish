-- file_io.y  —  v1.2: File I/O

y.fs.write("notes.txt", "Hello from Yolish!\n")
y.fs.append("notes.txt", "Second line.\n")

y.println(y.fs.exists("notes.txt"))
y.println(y.fs.exists("ghost.txt"))

let content = y.fs.read("notes.txt")
y.print(content)

y.println(y.fs.size("notes.txt"))
y.println(y.fs.is_dir("."))

y.fs.mkdir("test_dir")
y.println(y.fs.exists("test_dir"))

y.fs.rename("notes.txt", "notes_v2.txt")
y.println(y.fs.exists("notes_v2.txt"))

y.fs.delete("notes_v2.txt")
y.fs.delete("test_dir")
y.println(y.fs.exists("notes_v2.txt"))
