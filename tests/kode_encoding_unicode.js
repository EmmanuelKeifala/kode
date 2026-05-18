const encoding = require("kode:encoding")
const text = "hé 👋"
const bytes = encoding.encodeUtf8(text)
console.log("encoding-unicode", bytes instanceof Uint8Array, bytes.length, encoding.decodeUtf8(bytes) === text)
