const encoding = require("kode:encoding")
try {
  encoding.decodeUtf8(new Uint16Array([0x0061]))
} catch (err) {
  console.log("encoding-non-byte-view", err.code, err.operation)
}
