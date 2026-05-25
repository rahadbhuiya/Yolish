fn main() {
    y.print("Capability Test\n")

    -- Write with capability
    let w = cap.open("/tmp/yolish_out.txt", 2)
    cap.write(w, "Written by Yolish!\n")
    cap.close(w)
    y.print("Saved!\n")

    -- Check permission
    let r = cap.open("/tmp/yolish_out.txt", 1)
    let perm = cap.perm(r)
    y.print("Permission: ")
    y.print(perm)
    y.print("\n")
    cap.close(r)

    y.print("Done!\n")
}
