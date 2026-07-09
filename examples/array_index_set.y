-- array_index_set.y
-- arr[i] = value: direct mutation, inside loops, and passed through a
-- function (arrays share underlying storage, so mutations are visible
-- to the caller too).

fn zero_out(arr, i) {
    arr[i] = 0
}

fn main() {
    let nums = [10, 20, 30, 40, 50]
    nums[0] = 100
    nums[4] = 500
    y.println(nums)

    for i in 0..y.len(nums) {
        nums[i] = nums[i] * 2
    }
    y.println(nums)

    zero_out(nums, 2)
    y.println(nums)

    -- chained: assignment expression itself yields the assigned value
    let x = (nums[1] = 999)
    y.println(x)
    y.println(nums)
}
