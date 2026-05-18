const bytes = new TextEncoder().encode("hello")
const text = new TextDecoder().decode(bytes)
console.log("text-encoder-decoder", bytes instanceof Uint8Array, bytes.length, bytes[0], bytes[4], text)
