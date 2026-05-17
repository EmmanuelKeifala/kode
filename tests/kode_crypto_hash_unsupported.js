const crypto = require("kode:crypto")
try {
  crypto.hash("sha1", "hello")
} catch (err) {
  console.log("crypto-unsupported", err.code, err.operation)
}
