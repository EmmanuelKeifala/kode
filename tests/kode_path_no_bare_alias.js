try {
  require("path")
} catch (err) {
  console.log("path-no-bare", err.code, err.operation)
}
