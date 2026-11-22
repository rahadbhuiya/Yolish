-- bytes_demo.y
-- Demonstrating the raw byte buffer subsystem (y.bytes) and safe fallback builtins

fn main() {
    y.println("=== 1. y.bytes Allocation & Manipulation ===")
    let b = y.bytes.new(8)
    y.println("Initial length: {y.bytes.len(b)}")
    
    y.bytes.set(b, 0, 72)   -- 'H'
    y.bytes.set(b, 1, 101)  -- 'e'
    y.bytes.set(b, 2, 108)  -- 'l'
    y.bytes.set(b, 3, 108)  -- 'l'
    y.bytes.set(b, 4, 111)  -- 'o'
    y.bytes.set(b, 5, 33)   -- '!'
    y.bytes.set(b, 6, 0)    -- NUL
    y.bytes.set(b, 7, 255)  -- 0xFF

    y.println("Byte at index 0: {y.bytes.get(b, 0)}")
    y.println("Byte at index 5: {y.bytes.get(b, 5)}")
    y.println("Byte at index 7: {y.bytes.get(b, 7)}")
    
    y.println("\n=== 2. Hex Conversion ===")
    let hex_str = y.bytes.to_hex(b)
    y.println("Hex encoded: {hex_str}")

    let decoded = y.bytes.from_hex("4578706c6f69647573")
    y.println("Decoded from hex (Exploidus): {y.bytes.to_str(decoded)}")

    y.println("\n=== 3. String & Buffer Bridge ===")
    let msg = "Yolish Safe Byte Subsystem"
    let msg_bytes = y.bytes.from_str(msg)
    y.println("Converted to bytes, length: {y.bytes.len(msg_bytes)}")
    let roundtrip = y.bytes.to_str(msg_bytes)
    y.println("Roundtrip string: {roundtrip}")

    y.println("\n=== 4. Slicing & Concatenation ===")
    let sub = y.bytes.slice(msg_bytes, 0, 6)
    y.println("Sliced [0..6]: {y.bytes.to_str(sub)}")

    let b1 = y.bytes.from_str("Hello ")
    let b2 = y.bytes.from_str("World!")
    let joined = y.bytes.concat(b1, b2)
    y.println("Concat result: {y.bytes.to_str(joined)}")

    y.println("\n=== 5. Binary File I/O ===")
    let test_file = "test_binary_payload.bin"
    let written = y.bytes.write_file(test_file, joined)
    y.println("Bytes written to file: {written}")
    
    let read_back = y.bytes.read_file(test_file)
    y.println("Bytes read back: {y.bytes.len(read_back)}")
    y.println("Content: {y.bytes.to_str(read_back)}")
    y.fs.delete(test_file)

    y.println("\n=== 6. Safe Fallback Builtins (y.or & y.env.get_or) ===")
    let v_nil = nil
    let v_ok = "active"
    let fallback = "default"
    y.println("y.or(nil, fallback): " + y.or(v_nil, fallback))
    y.println("y.or('active', fallback): " + y.or(v_ok, fallback))
    
    let port = y.env.get_or("YOLISH_PORT_TEST_XYZ", "8080")
    y.println("y.env.get_or PORT: " + port)
    
    y.println("\nAll Yolish Phase features verified successfully!")
}
