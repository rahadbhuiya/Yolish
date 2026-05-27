-- arrays.y
-- Arrays: create, read, write, iterate, push

fn sum(arr) {
    var total = 0
    for n in arr {
        total = total + n
    }
    return total
}

fn main() {
    let nums = [10, 20, 30, 40, 50]

    y.print("Array  : ") y.println(nums)
    y.print("Length : ") y.println(y.len(nums))
    y.print("First  : ") y.println(nums[0])
    y.print("Last   : ") y.println(nums[4])

    nums[2] = 99
    y.print("After nums[2]=99: ") y.println(nums)

    y.push(nums, 60)
    y.print("After push(60)  : ") y.println(nums)

    y.print("Sum    : ") y.println(sum(nums))

    y.print("Range  : ")
    for i in 0..5 {
        y.print(i) y.print(" ")
    }
    y.print("\n")
}
