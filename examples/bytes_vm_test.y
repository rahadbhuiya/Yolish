-- bytes_vm_test.y
-- Test raw bytes operations directly under Bytecode VM

fn main() {
    let b = y.bytes.new(4)
    y.bytes.set(b, 0, 10)
    y.bytes.set(b, 1, 20)
    y.bytes.set(b, 2, 30)
    y.bytes.set(b, 3, 40)

    let len = y.bytes.len(b)
    y.println(len)

    let val0 = y.bytes.get(b, 0)
    y.println(val0)

    let hex = y.bytes.to_hex(b)
    y.println(hex)

    let dec = y.bytes.from_hex("0a141e28")
    let v2 = y.bytes.get(dec, 2)
    y.println(v2)

    let str_val = y.bytes.to_str(y.bytes.from_str("YolishVM"))
    y.println(str_val)

    let fallback_res = y.or(nil, "fallback_ok")
    y.println(fallback_res)
}
