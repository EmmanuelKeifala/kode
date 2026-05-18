try {
  require("encoding")
} catch (err) {
  console.log("encoding-no-bare", err.code, err.operation)
}
