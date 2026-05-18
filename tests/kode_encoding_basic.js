const encoding = require("kode:encoding")
const bytes = encoding.encodeUtf8("hello")
console.log("encoding-basic", bytes instanceof Uint8Array, bytes.length, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], encoding.decodeUtf8(bytes))
