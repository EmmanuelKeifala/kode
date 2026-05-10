try {
  require("./missing.js")
} catch (err) {
  console.log("module-missing", err.code, err.operation, err.path.endsWith("tests/modules/missing.js"))
}
