try {
  require("crypto")
} catch (err) {
  console.log("crypto-no-bare", err.code, err.operation)
}
