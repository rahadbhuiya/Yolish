fn main() {
    -- Array test
    let arr = [10, 20, 30, 40, 50]
    y.print("Array: ")
    y.print(arr)
    y.print("\n")
    y.print("arr[0]: ")
    y.print(arr[0])
    y.print("\n")
    y.print("arr[2]: ")
    y.print(arr[2])
    y.print("\n")
    y.print("len: ")
    y.print(y.len(arr))
    y.print("\n")

    -- Modify
    arr[1] = 99
    y.print("arr[1] after set: ")
    y.print(arr[1])
    y.print("\n")

    -- Loop through array
    var i = 0
    while i < y.len(arr) {
        y.print(arr[i])
        y.print(" ")
        i = i + 1
    }
    y.print("\n")
}
