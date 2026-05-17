const crypto = require("kode:crypto")
const digest = crypto.hash("sha256", "hello")
console.log("crypto-sha256", digest.algorithm, digest.hex)
